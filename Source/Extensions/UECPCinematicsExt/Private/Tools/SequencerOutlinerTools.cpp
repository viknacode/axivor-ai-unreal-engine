// Copyright 2026, BlueprintsLab, All rights reserved.

#include "Tools/SequencerOutlinerTools.h"
#include "Tools/BatchToolHelper.h"

#include "LevelSequence.h"
#include "LevelSequenceEditorBlueprintLibrary.h"
#include "MovieScene.h"
#include "MovieSceneMarkedFrame.h"
#include "MovieSceneSection.h"
#include "Sections/MovieSceneSubSection.h"
#include "MovieSceneBindingProxy.h"
#include "MovieScenePossessable.h"
#include "MovieSceneSpawnable.h"
#include "MovieSceneFolder.h"
#include "MovieSceneTrack.h"
#include "Decorations/MovieSceneMuteSoloDecoration.h"

#include "EditorAssetLibrary.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace SequencerOutlinerTools
{

namespace
{
	ULevelSequence* LoadSeq(const FString& Path, FString& OutError)
	{
		ULevelSequence* Seq = Cast<ULevelSequence>(UEditorAssetLibrary::LoadAsset(Path));
		if (!Seq)
		{
			OutError = FString::Printf(TEXT("LevelSequence not found at '%s'"), *Path);
		}
		return Seq;
	}

	UMovieScene* GetMovieScene(const FString& Path, FString& OutError)
	{
		ULevelSequence* Seq = LoadSeq(Path, OutError);
		if (!Seq) return nullptr;
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

	TSharedPtr<FJsonObject> SuccessObj(const FString& Key, const TSharedPtr<FJsonValue>& Value)
	{
		TSharedPtr<FJsonObject> R = MakeShared<FJsonObject>();
		R->SetBoolField(TEXT("success"), true);
		if (!Key.IsEmpty()) R->SetField(Key, Value);
		return R;
	}

	int32 ToggleNodePathInArray(TArray<FString>& Arr, const FString& NodePath, bool bWant)
	{
		const int32 ExistingIdx = Arr.IndexOfByKey(NodePath);
		if (bWant && ExistingIdx == INDEX_NONE) { Arr.Add(NodePath); return 1; }
		if (!bWant && ExistingIdx != INDEX_NONE) { Arr.RemoveAt(ExistingIdx); return 1; }
		return 0;
	}

	// UE 5.7+ (deprecated arrays ignored by Sequencer in 5.9): mute/solo live on per-object
	// UMovieSceneMuteSoloDecoration (binding / track / folder). Node paths are resolved by name:
	//   "<Binding>", "<Binding>.<Track>", "<Track>" (master track), "<Folder>[.<SubFolder>]", or a binding GUID.
	FString BindingLabel(UMovieScene* MS, const FGuid& Guid)
	{
		return MS->GetObjectDisplayName(Guid).ToString();
	}

	bool TrackMatches(const UMovieSceneTrack* Track, const FString& Name)
	{
		return Track && (Track->GetDisplayName().ToString().Equals(Name, ESearchCase::IgnoreCase)
			|| Track->GetName().Equals(Name, ESearchCase::IgnoreCase)
			|| Track->GetClass()->GetName().Equals(Name, ESearchCase::IgnoreCase));
	}

	UMovieSceneFolder* FindFolderByPath(TArrayView<UMovieSceneFolder* const> Folders, const TArray<FString>& Segs, int32 Index)
	{
		for (UMovieSceneFolder* F : Folders)
		{
			if (!F || !F->GetFolderName().ToString().Equals(Segs[Index], ESearchCase::IgnoreCase)) continue;
			if (Index == Segs.Num() - 1) return F;
			if (UMovieSceneFolder* Child = FindFolderByPath(F->GetChildFolders(), Segs, Index + 1)) return Child;
		}
		return nullptr;
	}

	// Applies the mute/solo flag; returns 1 when something changed, 0 when already set, -1 when not resolved.
	int32 ApplyMuteSolo(UMovieScene* MS, const FString& NodePath, bool bMute, bool bWant)
	{
		auto Apply = [bMute, bWant](UMovieSceneMuteSoloDecoration* D) -> int32
		{
			if (!D) return 0;
			const bool bCur = bMute ? D->IsMuted() : D->IsSoloed();
			if (bCur == bWant) return 0;
			if (bMute) D->SetMuted(bWant); else D->SetSoloed(bWant);
			return 1;
		};

		TArray<FString> Segs;
		NodePath.ParseIntoArray(Segs, TEXT("."), true);
		if (Segs.IsEmpty()) return -1;

		// Bindings (and their tracks).
		for (const FMovieSceneBinding& B : static_cast<const UMovieScene*>(MS)->GetBindings())
		{
			const FGuid Guid = B.GetObjectGuid();
			const FString Label = BindingLabel(MS, Guid);
			const bool bBindingHit = Segs[0].Equals(Label, ESearchCase::IgnoreCase) || Segs[0].Equals(Guid.ToString(), ESearchCase::IgnoreCase);
			if (!bBindingHit) continue;
			FMovieSceneBinding* Mutable = MS->FindBinding(Guid);
			if (!Mutable) continue;
			if (Segs.Num() == 1)
			{
				return Apply(Cast<UMovieSceneMuteSoloDecoration>(Mutable->GetOrCreateDecoration(
					UMovieSceneMuteSoloDecoration::StaticClass(), MS, [](UObject*) {})));
			}
			for (UMovieSceneTrack* T : Mutable->GetTracks())
				if (TrackMatches(T, Segs.Last())) return Apply(T->GetOrCreateDecoration<UMovieSceneMuteSoloDecoration>());
		}
		// Master tracks.
		if (Segs.Num() == 1)
			for (UMovieSceneTrack* T : MS->GetTracks())
				if (TrackMatches(T, Segs[0])) return Apply(T->GetOrCreateDecoration<UMovieSceneMuteSoloDecoration>());
		// Folders.
		if (UMovieSceneFolder* F = FindFolderByPath(MS->GetRootFolders(), Segs, 0))
			return Apply(F->GetOrCreateDecoration<UMovieSceneMuteSoloDecoration>());
		return -1;
	}

	const TCHAR* NodeNotFoundHint = TEXT("Use '<Binding>', '<Binding>.<Track>', '<Track>' (master track), '<Folder>[.<SubFolder>]' or a binding GUID; get_outliner_state lists current paths.");

	// Returns false when a mute/solo node path does not resolve to a binding, track or folder.
	bool SetOutlinerStateField(UMovieScene* MS, const FString& FieldName, const FString& NodePath, bool bWant, int32& OutChanged)
	{
		MS->Modify(false);
		if (FieldName == TEXT("mute") || FieldName == TEXT("solo"))
		{
			const int32 R = ApplyMuteSolo(MS, NodePath, FieldName == TEXT("mute"), bWant);
			if (R < 0) return false;
			OutChanged += R;
			return true;
		}
		if (FieldName == TEXT("pinned")) OutChanged += ToggleNodePathInArray(MS->GetEditorData().PinnedNodes, NodePath, bWant);
		return true;
	}

	// Collects the display paths of every muted / soloed binding, track and folder.
	void CollectMuteSolo(UMovieScene* MS, TArray<FString>& OutMuted, TArray<FString>& OutSolo)
	{
		auto Check = [&](const UMovieSceneMuteSoloDecoration* D, const FString& Path)
		{
			if (!D) return;
			if (D->IsMuted()) OutMuted.Add(Path);
			if (D->IsSoloed()) OutSolo.Add(Path);
		};
		for (const FMovieSceneBinding& B : static_cast<const UMovieScene*>(MS)->GetBindings())
		{
			const FString Label = BindingLabel(MS, B.GetObjectGuid());
			Check(B.FindDecoration<UMovieSceneMuteSoloDecoration>(), Label);
			for (const UMovieSceneTrack* T : B.GetTracks())
				if (T) Check(T->FindDecoration<UMovieSceneMuteSoloDecoration>(), Label + TEXT(".") + T->GetDisplayName().ToString());
		}
		for (const UMovieSceneTrack* T : MS->GetTracks())
			if (T) Check(T->FindDecoration<UMovieSceneMuteSoloDecoration>(), T->GetDisplayName().ToString());
		TFunction<void(TArrayView<UMovieSceneFolder* const>, const FString&)> Walk = [&](TArrayView<UMovieSceneFolder* const> Folders, const FString& Prefix)
		{
			for (const UMovieSceneFolder* F : Folders)
			{
				if (!F) continue;
				const FString Path = Prefix + F->GetFolderName().ToString();
				Check(F->FindDecoration<UMovieSceneMuteSoloDecoration>(), Path);
				Walk(F->GetChildFolders(), Path + TEXT("."));
			}
		};
		Walk(MS->GetRootFolders(), FString());
	}
}

void HandleAddMarkedFrameFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString SequencePath; Args->TryGetStringField(TEXT("sequence_path"), SequencePath);
	UMovieScene* MS = GetMovieScene(SequencePath, OutError); if (!MS) return;

	const TArray<TSharedPtr<FJsonValue>>* Marks = nullptr;
	if (BatchToolHelper::TryGetBatchItems(Args, TEXT("marks"), Marks))
	{
		BatchToolHelper::FBatchResultBuilder Batch;
		MS->Modify();
		for (int32 i = 0; i < Marks->Num(); ++i)
		{
			const TSharedPtr<FJsonObject> Entry = (*Marks)[i]->AsObject();
			if (!Entry.IsValid()) { Batch.AddFailure(i, TEXT("Invalid mark entry")); continue; }
			int32 Frame = 0; Entry->TryGetNumberField(TEXT("frame"), Frame);
			FString Label; Entry->TryGetStringField(TEXT("label"), Label);

			FMovieSceneMarkedFrame Mark;
			Mark.SetRangeStartAndEnd(FFrameNumber(Frame)); // UE 5.9: FrameNumber no longer drives the mark
			Mark.Label = Label;
			const int32 Idx = MS->AddMarkedFrame(Mark);

			TSharedPtr<FJsonObject> R = MakeShared<FJsonObject>();
			R->SetNumberField(TEXT("index"), Idx);
			R->SetNumberField(TEXT("frame"), Frame);
			R->SetStringField(TEXT("label"), Label);
			Batch.AddSuccess(i, R);
		}
		Batch.Finalize(OutJson);
		return;
	}

	int32 Frame = 0; Args->TryGetNumberField(TEXT("frame"), Frame);
	FString Label; Args->TryGetStringField(TEXT("label"), Label);

	MS->Modify();
	FMovieSceneMarkedFrame Mark;
	Mark.SetRangeStartAndEnd(FFrameNumber(Frame)); // UE 5.9: FrameNumber no longer drives the mark
	Mark.Label = Label;
	const int32 Idx = MS->AddMarkedFrame(Mark);

	TSharedPtr<FJsonObject> R = MakeShared<FJsonObject>();
	R->SetBoolField(TEXT("success"), true);
	R->SetNumberField(TEXT("index"), Idx);
	R->SetNumberField(TEXT("frame"), Frame);
	R->SetStringField(TEXT("label"), Label);
	OutJson = SerializeJson(R);
}

void HandleDeleteMarkedFrameFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString SequencePath; Args->TryGetStringField(TEXT("sequence_path"), SequencePath);
	UMovieScene* MS = GetMovieScene(SequencePath, OutError); if (!MS) return;

	const TArray<TSharedPtr<FJsonValue>>* Items = nullptr;
	if (BatchToolHelper::TryGetBatchItems(Args, TEXT("indices"), Items))
	{
		BatchToolHelper::FBatchResultBuilder Batch;
		MS->Modify();

		struct FRequest { int32 InputPos; int32 Index; };
		TArray<FRequest> Requests;
		const int32 OriginalCount = MS->GetMarkedFrames().Num();
		for (int32 i = 0; i < Items->Num(); ++i)
		{
			int32 Idx = -1;
			if ((*Items)[i]->Type == EJson::Number) Idx = static_cast<int32>((*Items)[i]->AsNumber());
			else if (TSharedPtr<FJsonObject> Obj = (*Items)[i]->AsObject()) Obj->TryGetNumberField(TEXT("index"), Idx);
			if (Idx < 0 || Idx >= OriginalCount)
			{
				Batch.AddFailure(i, FString::Printf(TEXT("Index %d out of range [0,%d)"), Idx, OriginalCount));
				continue;
			}
			Requests.Add({i, Idx});
		}
		Requests.Sort([](const FRequest& A, const FRequest& B) { return A.Index > B.Index; });

		TSet<int32> Seen;
		for (const FRequest& Req : Requests)
		{
			if (Seen.Contains(Req.Index))
			{
				Batch.AddFailure(Req.InputPos, FString::Printf(TEXT("Index %d listed twice in same batch"), Req.Index));
				continue;
			}
			Seen.Add(Req.Index);
			MS->DeleteMarkedFrame(Req.Index);
			TSharedPtr<FJsonObject> R = MakeShared<FJsonObject>();
			R->SetNumberField(TEXT("index"), Req.Index);
			Batch.AddSuccess(Req.InputPos, R);
		}
		Batch.Finalize(OutJson);
		return;
	}

	int32 Idx = -1; Args->TryGetNumberField(TEXT("index"), Idx);
	const int32 Count = MS->GetMarkedFrames().Num();
	if (Idx < 0 || Idx >= Count)
	{
		OutError = FString::Printf(TEXT("Index %d out of range [0,%d)"), Idx, Count);
		return;
	}
	MS->Modify();
	MS->DeleteMarkedFrame(Idx);

	TSharedPtr<FJsonObject> R = MakeShared<FJsonObject>();
	R->SetBoolField(TEXT("success"), true);
	R->SetNumberField(TEXT("index"), Idx);
	OutJson = SerializeJson(R);
}

void HandleDeleteAllMarkedFramesFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString SequencePath; Args->TryGetStringField(TEXT("sequence_path"), SequencePath);
	UMovieScene* MS = GetMovieScene(SequencePath, OutError); if (!MS) return;

	const int32 Removed = MS->GetMarkedFrames().Num();
	MS->Modify();
	MS->DeleteMarkedFrames();

	TSharedPtr<FJsonObject> R = MakeShared<FJsonObject>();
	R->SetBoolField(TEXT("success"), true);
	R->SetNumberField(TEXT("removed"), Removed);
	OutJson = SerializeJson(R);
}

void HandleFindMarkedFrameByLabelFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString SequencePath; Args->TryGetStringField(TEXT("sequence_path"), SequencePath);
	UMovieScene* MS = GetMovieScene(SequencePath, OutError); if (!MS) return;
	FString Label; Args->TryGetStringField(TEXT("label"), Label);

	const int32 Idx = MS->FindMarkedFrameByLabel(Label);

	TSharedPtr<FJsonObject> R = MakeShared<FJsonObject>();
	R->SetBoolField(TEXT("success"), true);
	R->SetNumberField(TEXT("index"), Idx);
	if (Idx != INDEX_NONE)
	{
		const FMovieSceneMarkedFrame& Mark = MS->GetMarkedFrames()[Idx];
		R->SetNumberField(TEXT("frame"), Mark.GetStartFrame().Value);
		R->SetStringField(TEXT("label"), Mark.Label);
	}
	OutJson = SerializeJson(R);
}

void HandleGetMarkedFramesFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString SequencePath; Args->TryGetStringField(TEXT("sequence_path"), SequencePath);
	UMovieScene* MS = GetMovieScene(SequencePath, OutError); if (!MS) return;

	TArray<TSharedPtr<FJsonValue>> Arr;
	const TArray<FMovieSceneMarkedFrame>& Marks = MS->GetMarkedFrames();
	for (int32 i = 0; i < Marks.Num(); ++i)
	{
		const FMovieSceneMarkedFrame& Mark = Marks[i];
		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetNumberField(TEXT("index"), i);
		Entry->SetNumberField(TEXT("frame"), Mark.GetStartFrame().Value);
		Entry->SetStringField(TEXT("label"), Mark.Label);
		Entry->SetBoolField(TEXT("is_determinism_fence"), Mark.bIsDeterminismFence);
		Arr.Add(MakeShared<FJsonValueObject>(Entry));
	}

	TSharedPtr<FJsonObject> R = MakeShared<FJsonObject>();
	R->SetBoolField(TEXT("success"), true);
	R->SetNumberField(TEXT("count"), Marks.Num());
	R->SetArrayField(TEXT("marks"), Arr);
	OutJson = SerializeJson(R);
}

void HandleSetMarkedFramesLockedFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString SequencePath; Args->TryGetStringField(TEXT("sequence_path"), SequencePath);
	UMovieScene* MS = GetMovieScene(SequencePath, OutError); if (!MS) return;
	bool bLocked = false; Args->TryGetBoolField(TEXT("locked"), bLocked);

	MS->Modify();
	MS->SetMarkedFramesLocked(bLocked);

	TSharedPtr<FJsonObject> R = MakeShared<FJsonObject>();
	R->SetBoolField(TEXT("success"), true);
	R->SetBoolField(TEXT("locked"), bLocked);
	OutJson = SerializeJson(R);
}

namespace
{
	void HandleSetNodeStateField(const FString& FieldName, const FString& BoolKey,
		const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError)
	{
		if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
		FString SequencePath; Args->TryGetStringField(TEXT("sequence_path"), SequencePath);
		UMovieScene* MS = GetMovieScene(SequencePath, OutError); if (!MS) return;

		const TArray<TSharedPtr<FJsonValue>>* Items = nullptr;
		if (BatchToolHelper::TryGetBatchItems(Args, TEXT("items"), Items))
		{
			BatchToolHelper::FBatchResultBuilder Batch;
			int32 Changed = 0;
			for (int32 i = 0; i < Items->Num(); ++i)
			{
				const TSharedPtr<FJsonObject> Entry = (*Items)[i]->AsObject();
				if (!Entry.IsValid()) { Batch.AddFailure(i, TEXT("Invalid item")); continue; }
				FString NodePath; Entry->TryGetStringField(TEXT("node_path"), NodePath);
				bool bWant = false; Entry->TryGetBoolField(BoolKey, bWant);
				if (NodePath.IsEmpty()) { Batch.AddFailure(i, TEXT("node_path required")); continue; }
				if (!SetOutlinerStateField(MS, FieldName, NodePath, bWant, Changed))
				{
					Batch.AddFailure(i, FString::Printf(TEXT("Node '%s' not found. %s"), *NodePath, NodeNotFoundHint));
					continue;
				}
				TSharedPtr<FJsonObject> R = MakeShared<FJsonObject>();
				R->SetStringField(TEXT("node_path"), NodePath);
				R->SetBoolField(BoolKey, bWant);
				Batch.AddSuccess(i, R);
			}
			Batch.Finalize(OutJson);
			return;
		}

		FString NodePath; Args->TryGetStringField(TEXT("node_path"), NodePath);
		bool bWant = false; Args->TryGetBoolField(BoolKey, bWant);
		if (NodePath.IsEmpty()) { OutError = TEXT("node_path required"); return; }
		int32 Changed = 0;
		if (!SetOutlinerStateField(MS, FieldName, NodePath, bWant, Changed))
		{
			OutError = FString::Printf(TEXT("Node '%s' not found in '%s'. %s"), *NodePath, *SequencePath, NodeNotFoundHint);
			return;
		}

		TSharedPtr<FJsonObject> R = MakeShared<FJsonObject>();
		R->SetBoolField(TEXT("success"), true);
		R->SetStringField(TEXT("node_path"), NodePath);
		R->SetBoolField(BoolKey, bWant);
		R->SetNumberField(TEXT("changed"), Changed);
		OutJson = SerializeJson(R);
	}
}

void HandleSetNodeMutedFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError)
{
	HandleSetNodeStateField(TEXT("mute"), TEXT("muted"), Args, OutJson, OutError);
}

void HandleSetNodeSoloFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError)
{
	HandleSetNodeStateField(TEXT("solo"), TEXT("solo"), Args, OutJson, OutError);
}

void HandleSetNodePinnedFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError)
{
	HandleSetNodeStateField(TEXT("pinned"), TEXT("pinned"), Args, OutJson, OutError);
}

void HandleGetOutlinerStateFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString SequencePath; Args->TryGetStringField(TEXT("sequence_path"), SequencePath);
	UMovieScene* MS = GetMovieScene(SequencePath, OutError); if (!MS) return;

	auto ToJsonStringArray = [](const TArray<FString>& Arr) -> TArray<TSharedPtr<FJsonValue>>
	{
		TArray<TSharedPtr<FJsonValue>> Out;
		for (const FString& S : Arr) Out.Add(MakeShared<FJsonValueString>(S));
		return Out;
	};

	TSharedPtr<FJsonObject> R = MakeShared<FJsonObject>();
	R->SetBoolField(TEXT("success"), true);
	TArray<FString> Muted, Solo;
	CollectMuteSolo(MS, Muted, Solo);
	R->SetArrayField(TEXT("muted"),  ToJsonStringArray(Muted));
	R->SetArrayField(TEXT("solo"),   ToJsonStringArray(Solo));
	R->SetArrayField(TEXT("pinned"), ToJsonStringArray(MS->GetEditorData().PinnedNodes));
	R->SetBoolField(TEXT("playback_range_locked"), MS->IsPlaybackRangeLocked());
	R->SetBoolField(TEXT("marked_frames_locked"),  MS->AreMarkedFramesLocked());
	OutJson = SerializeJson(R);
}

void HandleSetSectionLockedFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString SequencePath; Args->TryGetStringField(TEXT("sequence_path"), SequencePath);
	UMovieScene* MS = GetMovieScene(SequencePath, OutError); if (!MS) return;
	int32 TrackIndex = -1; Args->TryGetNumberField(TEXT("track_index"), TrackIndex);
	int32 SectionIndex = -1; Args->TryGetNumberField(TEXT("section_index"), SectionIndex);
	bool bLocked = false; Args->TryGetBoolField(TEXT("locked"), bLocked);
	FString BindingLabel; Args->TryGetStringField(TEXT("binding_label"), BindingLabel);

	UMovieSceneTrack* Track = nullptr;
	if (BindingLabel.IsEmpty())
	{
		const TArray<UMovieSceneTrack*>& Master = MS->GetTracks();
		if (!Master.IsValidIndex(TrackIndex)) { OutError = FString::Printf(TEXT("track_index %d out of range [0,%d) on master tracks"), TrackIndex, Master.Num()); return; }
		Track = Master[TrackIndex];
	}
	else
	{
		FGuid Found;
		for (int32 i = 0; i < MS->GetPossessableCount(); ++i)
		{
			if (MS->GetPossessable(i).GetName() == BindingLabel) { Found = MS->GetPossessable(i).GetGuid(); break; }
		}
		if (!Found.IsValid())
		{
			for (int32 i = 0; i < MS->GetSpawnableCount(); ++i)
			{
				if (MS->GetSpawnable(i).GetName() == BindingLabel) { Found = MS->GetSpawnable(i).GetGuid(); break; }
			}
		}
		if (!Found.IsValid()) { OutError = FString::Printf(TEXT("No binding labelled '%s' found"), *BindingLabel); return; }
		const TArray<UMovieSceneTrack*> BindingTracks = MS->FindTracks(UMovieSceneTrack::StaticClass(), Found);
		if (!BindingTracks.IsValidIndex(TrackIndex)) { OutError = FString::Printf(TEXT("track_index %d out of range [0,%d) on binding '%s'"), TrackIndex, BindingTracks.Num(), *BindingLabel); return; }
		Track = BindingTracks[TrackIndex];
	}
	if (!Track) { OutError = TEXT("Track not found"); return; }

	const TArray<UMovieSceneSection*>& Sections = Track->GetAllSections();
	if (!Sections.IsValidIndex(SectionIndex)) { OutError = FString::Printf(TEXT("section_index %d out of range [0,%d)"), SectionIndex, Sections.Num()); return; }
	UMovieSceneSection* Section = Sections[SectionIndex];

	Section->SetIsLocked(bLocked);

	TSharedPtr<FJsonObject> R = MakeShared<FJsonObject>();
	R->SetBoolField(TEXT("success"), true);
	R->SetBoolField(TEXT("locked"), bLocked);
	OutJson = SerializeJson(R);
}

void HandleSetPlaybackRangeLockedFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString SequencePath; Args->TryGetStringField(TEXT("sequence_path"), SequencePath);
	UMovieScene* MS = GetMovieScene(SequencePath, OutError); if (!MS) return;
	bool bLocked = false; Args->TryGetBoolField(TEXT("locked"), bLocked);

	MS->Modify();
	MS->SetPlaybackRangeLocked(bLocked);

	TSharedPtr<FJsonObject> R = MakeShared<FJsonObject>();
	R->SetBoolField(TEXT("success"), true);
	R->SetBoolField(TEXT("locked"), bLocked);
	OutJson = SerializeJson(R);
}

void HandleFocusSubSequenceFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString SequencePath; Args->TryGetStringField(TEXT("sequence_path"), SequencePath);
	FString SubSequencePath; Args->TryGetStringField(TEXT("sub_sequence_path"), SubSequencePath);
	if (SubSequencePath.IsEmpty()) { OutError = TEXT("sub_sequence_path required"); return; }

	ULevelSequence* RootSeq = LoadSeq(SequencePath, OutError); if (!RootSeq) return;
	ULevelSequenceEditorBlueprintLibrary::OpenLevelSequence(RootSeq);

	UMovieSceneSubSection* TargetSub = nullptr;
	for (TObjectIterator<UMovieSceneSubSection> It; It; ++It)
	{
		UMovieSceneSubSection* Sub = *It;
		if (!IsValid(Sub) || !Sub->GetSequence()) continue;
		if (Sub->GetSequence()->GetPathName() == SubSequencePath ||
			Sub->GetSequence()->GetPathName().Contains(SubSequencePath))
		{
			TargetSub = Sub;
			break;
		}
	}
	if (!TargetSub) { OutError = FString::Printf(TEXT("Sub-section pointing at '%s' not found"), *SubSequencePath); return; }

	ULevelSequenceEditorBlueprintLibrary::FocusLevelSequence(TargetSub);

	TSharedPtr<FJsonObject> R = MakeShared<FJsonObject>();
	R->SetBoolField(TEXT("success"), true);
	R->SetStringField(TEXT("focused_sequence_path"), SubSequencePath);
	OutJson = SerializeJson(R);
}

void HandleFocusParentSequenceFromArgs(const TSharedPtr<FJsonObject>& , FString& OutJson, FString& )
{
	ULevelSequenceEditorBlueprintLibrary::FocusParentSequence();

	TSharedPtr<FJsonObject> R = MakeShared<FJsonObject>();
	R->SetBoolField(TEXT("success"), true);
	OutJson = SerializeJson(R);
}

void HandleGetSubSequenceHierarchyFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString SequencePath; Args->TryGetStringField(TEXT("sequence_path"), SequencePath);
	ULevelSequence* RootSeq = LoadSeq(SequencePath, OutError); if (!RootSeq) return;

	TArray<TSharedPtr<FJsonValue>> Arr;
	ULevelSequenceEditorBlueprintLibrary::OpenLevelSequence(RootSeq);
	const TArray<UMovieSceneSubSection*> Hierarchy = ULevelSequenceEditorBlueprintLibrary::GetSubSequenceHierarchy();
	for (UMovieSceneSubSection* Sub : Hierarchy)
	{
		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		if (Sub && Sub->GetSequence())
		{
			Entry->SetStringField(TEXT("sequence_path"), Sub->GetSequence()->GetPathName());
			Entry->SetNumberField(TEXT("start_frame"), Sub->HasStartFrame() ? Sub->GetInclusiveStartFrame().Value : 0);
			Entry->SetNumberField(TEXT("end_frame"), Sub->HasEndFrame() ? Sub->GetExclusiveEndFrame().Value : 0);
		}
		Arr.Add(MakeShared<FJsonValueObject>(Entry));
	}

	TSharedPtr<FJsonObject> R = MakeShared<FJsonObject>();
	R->SetBoolField(TEXT("success"), true);
	R->SetStringField(TEXT("root_sequence_path"), SequencePath);
	R->SetArrayField(TEXT("sub_sections"), Arr);
	OutJson = SerializeJson(R);
}

}
