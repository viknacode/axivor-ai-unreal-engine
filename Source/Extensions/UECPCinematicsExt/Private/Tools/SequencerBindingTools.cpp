// Copyright 2026, BlueprintsLab, All rights reserved.

#include "Tools/SequencerBindingTools.h"
#include "Tools/BatchToolHelper.h"

#include "LevelSequence.h"
#include "LevelSequenceEditorBlueprintLibrary.h"
#include "LevelSequenceEditorSubsystem.h"
#include "MovieScene.h"
#include "MovieScenePossessable.h"
#include "MovieSceneSpawnable.h"
#include "MovieSceneObjectBindingID.h"
#include "MovieSceneBindingProxy.h"
#include "Misc/EngineVersionComparison.h"
#if !UE_VERSION_OLDER_THAN(5, 5, 0)
#include "Bindings/MovieSceneCustomBinding.h"
#endif

#include "EditorAssetLibrary.h"
#include "Editor.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "Subsystems/EditorActorSubsystem.h"
#include "UObject/UObjectIterator.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace SequencerBindingTools
{

namespace
{
	ULevelSequence* LoadSeq(const FString& Path, FString& OutError)
	{
		ULevelSequence* Seq = Cast<ULevelSequence>(UEditorAssetLibrary::LoadAsset(Path));
		if (!Seq) OutError = FString::Printf(TEXT("LevelSequence not found at '%s'"), *Path);
		return Seq;
	}

	UMovieScene* GetMovieScene(const FString& Path, FString& OutError)
	{
		ULevelSequence* Seq = LoadSeq(Path, OutError); if (!Seq) return nullptr;
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
		for (int32 i = 0; i < MS->GetPossessableCount(); ++i)
		{
			const FMovieScenePossessable& P = MS->GetPossessable(i);
			if (P.GetName() == Label) return P.GetGuid();
		}
		for (int32 i = 0; i < MS->GetSpawnableCount(); ++i)
		{
			const FMovieSceneSpawnable& S = MS->GetSpawnable(i);
			if (S.GetName() == Label) return S.GetGuid();
		}
		return FGuid();
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
			const FGuid G = FindBindingGuidByLabel(MS, Label);
			if (!G.IsValid()) OutError = FString::Printf(TEXT("binding '%s' not found"), *Label);
			return G;
		}
		OutError = TEXT("binding_guid or binding_label required");
		return FGuid();
	}

	FGuid ResolveBindingGuidFromEntry(UMovieScene* MS, const TSharedPtr<FJsonObject>& Entry, FString& OutError)
	{
		FString GuidStr;
		if (Entry->TryGetStringField(TEXT("binding_guid"), GuidStr) && !GuidStr.IsEmpty())
		{
			FGuid G;
			if (FGuid::Parse(GuidStr, G)) return G;
			OutError = FString::Printf(TEXT("invalid binding_guid '%s'"), *GuidStr);
			return FGuid();
		}
		FString Label;
		if (Entry->TryGetStringField(TEXT("binding_label"), Label) && !Label.IsEmpty())
		{
			const FGuid G = FindBindingGuidByLabel(MS, Label);
			if (!G.IsValid()) OutError = FString::Printf(TEXT("binding '%s' not found"), *Label);
			return G;
		}
		OutError = TEXT("binding_guid or binding_label required");
		return FGuid();
	}

	FString GetBindingLabelByGuid(UMovieScene* MS, const FGuid& Guid)
	{
		for (int32 i = 0; i < MS->GetPossessableCount(); ++i)
		{
			const FMovieScenePossessable& P = MS->GetPossessable(i);
			if (P.GetGuid() == Guid) return P.GetName();
		}
		for (int32 i = 0; i < MS->GetSpawnableCount(); ++i)
		{
			const FMovieSceneSpawnable& S = MS->GetSpawnable(i);
			if (S.GetGuid() == Guid) return S.GetName();
		}
		return FString();
	}

	ULevelSequenceEditorSubsystem* GetEditorSubsystem(FString& OutError)
	{
		ULevelSequenceEditorSubsystem* Sub = GEditor ? GEditor->GetEditorSubsystem<ULevelSequenceEditorSubsystem>() : nullptr;
		if (!Sub) OutError = TEXT("LevelSequenceEditorSubsystem not available");
		return Sub;
	}

	bool EnsureSequenceFocused(const FString& SequencePath, ULevelSequence*& OutSeq, FString& OutError)
	{
		OutSeq = LoadSeq(SequencePath, OutError);
		if (!OutSeq) return false;
		ULevelSequenceEditorBlueprintLibrary::OpenLevelSequence(OutSeq);
		return true;
	}

	AActor* FindActorByLabel(const FString& ActorLabel)
	{
		if (!GEditor || !GEditor->GetEditorWorldContext().World()) return nullptr;
		for (FActorIterator It(GEditor->GetEditorWorldContext().World()); It; ++It)
		{
			AActor* A = *It;
			if (A && A->GetActorLabel() == ActorLabel) return A;
		}
		return nullptr;
	}

	UClass* ResolveActorClass(const FString& ClassPath)
	{
		if (UClass* Direct = FindObject<UClass>(nullptr, *ClassPath)) return Direct;
		if (UClass* Loaded = LoadObject<UClass>(nullptr, *ClassPath)) return Loaded;
		const FString WithSuffix = ClassPath.EndsWith(TEXT("_C")) ? ClassPath : ClassPath + TEXT("_C");
		return LoadObject<UClass>(nullptr, *WithSuffix);
	}
}

void HandleTagBindingFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString SequencePath; Args->TryGetStringField(TEXT("sequence_path"), SequencePath);
	UMovieScene* MS = GetMovieScene(SequencePath, OutError); if (!MS) return;

	const TArray<TSharedPtr<FJsonValue>>* Items = nullptr;
	if (BatchToolHelper::TryGetBatchItems(Args, TEXT("items"), Items))
	{
		BatchToolHelper::FBatchResultBuilder Batch;
		MS->Modify();
		for (int32 i = 0; i < Items->Num(); ++i)
		{
			const TSharedPtr<FJsonObject> Entry = (*Items)[i]->AsObject();
			if (!Entry.IsValid()) { Batch.AddFailure(i, TEXT("Invalid item")); continue; }
			FString EntryErr;
			const FGuid Guid = ResolveBindingGuidFromEntry(MS, Entry, EntryErr);
			if (!Guid.IsValid()) { Batch.AddFailure(i, EntryErr); continue; }
			FString Tag; Entry->TryGetStringField(TEXT("tag"), Tag);
			if (Tag.IsEmpty()) { Batch.AddFailure(i, TEXT("tag required")); continue; }
			MS->TagBinding(FName(*Tag), UE::MovieScene::FFixedObjectBindingID(Guid, MovieSceneSequenceID::Root));
			TSharedPtr<FJsonObject> R = MakeShared<FJsonObject>();
			R->SetStringField(TEXT("binding_label"), GetBindingLabelByGuid(MS, Guid));
			R->SetStringField(TEXT("tag"), Tag);
			Batch.AddSuccess(i, R);
		}
		Batch.Finalize(OutJson);
		return;
	}

	const FGuid Guid = ResolveBindingGuid(MS, Args, OutError);
	if (!Guid.IsValid()) return;
	FString Tag; Args->TryGetStringField(TEXT("tag"), Tag);
	if (Tag.IsEmpty()) { OutError = TEXT("tag required"); return; }

	MS->Modify();
	MS->TagBinding(FName(*Tag), UE::MovieScene::FFixedObjectBindingID(Guid, MovieSceneSequenceID::Root));

	TSharedPtr<FJsonObject> R = MakeShared<FJsonObject>();
	R->SetBoolField(TEXT("success"), true);
	R->SetStringField(TEXT("binding_label"), GetBindingLabelByGuid(MS, Guid));
	R->SetStringField(TEXT("tag"), Tag);
	OutJson = SerializeJson(R);
}

void HandleUntagBindingFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString SequencePath; Args->TryGetStringField(TEXT("sequence_path"), SequencePath);
	UMovieScene* MS = GetMovieScene(SequencePath, OutError); if (!MS) return;

	const TArray<TSharedPtr<FJsonValue>>* Items = nullptr;
	if (BatchToolHelper::TryGetBatchItems(Args, TEXT("items"), Items))
	{
		BatchToolHelper::FBatchResultBuilder Batch;
		MS->Modify();
		for (int32 i = 0; i < Items->Num(); ++i)
		{
			const TSharedPtr<FJsonObject> Entry = (*Items)[i]->AsObject();
			if (!Entry.IsValid()) { Batch.AddFailure(i, TEXT("Invalid item")); continue; }
			FString EntryErr;
			const FGuid Guid = ResolveBindingGuidFromEntry(MS, Entry, EntryErr);
			if (!Guid.IsValid()) { Batch.AddFailure(i, EntryErr); continue; }
			FString Tag; Entry->TryGetStringField(TEXT("tag"), Tag);
			MS->UntagBinding(FName(*Tag), UE::MovieScene::FFixedObjectBindingID(Guid, MovieSceneSequenceID::Root));
			TSharedPtr<FJsonObject> R = MakeShared<FJsonObject>();
			R->SetStringField(TEXT("binding_label"), GetBindingLabelByGuid(MS, Guid));
			R->SetStringField(TEXT("tag"), Tag);
			Batch.AddSuccess(i, R);
		}
		Batch.Finalize(OutJson);
		return;
	}

	const FGuid Guid = ResolveBindingGuid(MS, Args, OutError);
	if (!Guid.IsValid()) return;
	FString Tag; Args->TryGetStringField(TEXT("tag"), Tag);

	MS->Modify();
	MS->UntagBinding(FName(*Tag), UE::MovieScene::FFixedObjectBindingID(Guid, MovieSceneSequenceID::Root));

	TSharedPtr<FJsonObject> R = MakeShared<FJsonObject>();
	R->SetBoolField(TEXT("success"), true);
	OutJson = SerializeJson(R);
}

void HandleFindBindingByTagFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString SequencePath; Args->TryGetStringField(TEXT("sequence_path"), SequencePath);
	UMovieScene* MS = GetMovieScene(SequencePath, OutError); if (!MS) return;
	FString Tag; Args->TryGetStringField(TEXT("tag"), Tag);

	const TMap<FName, FMovieSceneObjectBindingIDs>& All = MS->AllTaggedBindings();
	const FMovieSceneObjectBindingIDs* Found = All.Find(FName(*Tag));

	TSharedPtr<FJsonObject> R = MakeShared<FJsonObject>();
	R->SetBoolField(TEXT("success"), true);
	if (Found && Found->IDs.Num() > 0)
	{
		const FGuid FirstGuid = Found->IDs[0].GetGuid();
		R->SetStringField(TEXT("binding_label"), GetBindingLabelByGuid(MS, FirstGuid));
		R->SetStringField(TEXT("guid"), FirstGuid.ToString(EGuidFormats::DigitsWithHyphens));
	}
	else
	{
		R->SetStringField(TEXT("binding_label"), TEXT(""));
	}
	OutJson = SerializeJson(R);
}

void HandleFindBindingsByTagFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString SequencePath; Args->TryGetStringField(TEXT("sequence_path"), SequencePath);
	UMovieScene* MS = GetMovieScene(SequencePath, OutError); if (!MS) return;
	FString Tag; Args->TryGetStringField(TEXT("tag"), Tag);

	const TMap<FName, FMovieSceneObjectBindingIDs>& All = MS->AllTaggedBindings();
	TArray<TSharedPtr<FJsonValue>> Arr;
	if (const FMovieSceneObjectBindingIDs* Found = All.Find(FName(*Tag)))
	{
		for (const FMovieSceneObjectBindingID& BID : Found->IDs)
		{
			TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
			const FGuid Guid = BID.GetGuid();
			Entry->SetStringField(TEXT("binding_label"), GetBindingLabelByGuid(MS, Guid));
			Entry->SetStringField(TEXT("guid"), Guid.ToString(EGuidFormats::DigitsWithHyphens));
			Arr.Add(MakeShared<FJsonValueObject>(Entry));
		}
	}

	TSharedPtr<FJsonObject> R = MakeShared<FJsonObject>();
	R->SetBoolField(TEXT("success"), true);
	R->SetArrayField(TEXT("bindings"), Arr);
	OutJson = SerializeJson(R);
}

void HandleGetAllBindingTagsFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString SequencePath; Args->TryGetStringField(TEXT("sequence_path"), SequencePath);
	UMovieScene* MS = GetMovieScene(SequencePath, OutError); if (!MS) return;

	TArray<TSharedPtr<FJsonValue>> Arr;
	for (const auto& Pair : MS->AllTaggedBindings())
	{
		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("tag"), Pair.Key.ToString());
		TArray<TSharedPtr<FJsonValue>> BindingArr;
		for (const FMovieSceneObjectBindingID& BID : Pair.Value.IDs)
		{
			TSharedPtr<FJsonObject> B = MakeShared<FJsonObject>();
			B->SetStringField(TEXT("binding_label"), GetBindingLabelByGuid(MS, BID.GetGuid()));
			B->SetStringField(TEXT("guid"), BID.GetGuid().ToString(EGuidFormats::DigitsWithHyphens));
			BindingArr.Add(MakeShared<FJsonValueObject>(B));
		}
		Entry->SetArrayField(TEXT("bindings"), BindingArr);
		Arr.Add(MakeShared<FJsonValueObject>(Entry));
	}

	TSharedPtr<FJsonObject> R = MakeShared<FJsonObject>();
	R->SetBoolField(TEXT("success"), true);
	R->SetArrayField(TEXT("tags"), Arr);
	OutJson = SerializeJson(R);
}

void HandleGetBindingTagsFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString SequencePath; Args->TryGetStringField(TEXT("sequence_path"), SequencePath);
	UMovieScene* MS = GetMovieScene(SequencePath, OutError); if (!MS) return;
	const FGuid Guid = ResolveBindingGuid(MS, Args, OutError);
	if (!Guid.IsValid()) return;

	TArray<TSharedPtr<FJsonValue>> Arr;
	for (const auto& Pair : MS->AllTaggedBindings())
	{
		for (const FMovieSceneObjectBindingID& BID : Pair.Value.IDs)
		{
			if (BID.GetGuid() == Guid)
			{
				Arr.Add(MakeShared<FJsonValueString>(Pair.Key.ToString()));
				break;
			}
		}
	}

	TSharedPtr<FJsonObject> R = MakeShared<FJsonObject>();
	R->SetBoolField(TEXT("success"), true);
	R->SetStringField(TEXT("binding_label"), GetBindingLabelByGuid(MS, Guid));
	R->SetArrayField(TEXT("tags"), Arr);
	OutJson = SerializeJson(R);
}

void HandleRemoveBindingTagFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString SequencePath; Args->TryGetStringField(TEXT("sequence_path"), SequencePath);
	UMovieScene* MS = GetMovieScene(SequencePath, OutError); if (!MS) return;
	FString Tag; Args->TryGetStringField(TEXT("tag"), Tag);

	MS->Modify();
	MS->RemoveTag(FName(*Tag));

	TSharedPtr<FJsonObject> R = MakeShared<FJsonObject>();
	R->SetBoolField(TEXT("success"), true);
	R->SetStringField(TEXT("tag"), Tag);
	OutJson = SerializeJson(R);
}

void HandleFixActorReferencesFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString SequencePath; Args->TryGetStringField(TEXT("sequence_path"), SequencePath);
	ULevelSequence* Seq = nullptr;
	if (!EnsureSequenceFocused(SequencePath, Seq, OutError)) return;
	ULevelSequenceEditorSubsystem* Sub = GetEditorSubsystem(OutError); if (!Sub) return;

	Sub->FixActorReferences();

	TSharedPtr<FJsonObject> R = MakeShared<FJsonObject>();
	R->SetBoolField(TEXT("success"), true);
	OutJson = SerializeJson(R);
}

void HandleRebindComponentFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString SequencePath; Args->TryGetStringField(TEXT("sequence_path"), SequencePath);
	ULevelSequence* Seq = nullptr;
	if (!EnsureSequenceFocused(SequencePath, Seq, OutError)) return;
	UMovieScene* MS = Seq->GetMovieScene(); if (!MS) { OutError = TEXT("No MovieScene"); return; }

	const FGuid Guid = ResolveBindingGuid(MS, Args, OutError);
	if (!Guid.IsValid()) return;
	FString ComponentName; Args->TryGetStringField(TEXT("component_name"), ComponentName);

	ULevelSequenceEditorSubsystem* Sub = GetEditorSubsystem(OutError); if (!Sub) return;
	FMovieSceneBindingProxy Proxy(Guid, Seq);
	Sub->RebindComponent({Proxy}, FName(*ComponentName));

	TSharedPtr<FJsonObject> R = MakeShared<FJsonObject>();
	R->SetBoolField(TEXT("success"), true);
	OutJson = SerializeJson(R);
}

void HandleRemoveInvalidBindingsFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString SequencePath; Args->TryGetStringField(TEXT("sequence_path"), SequencePath);
	ULevelSequence* Seq = nullptr;
	if (!EnsureSequenceFocused(SequencePath, Seq, OutError)) return;
	UMovieScene* MS = Seq->GetMovieScene(); if (!MS) { OutError = TEXT("No MovieScene"); return; }

	ULevelSequenceEditorSubsystem* Sub = GetEditorSubsystem(OutError); if (!Sub) return;

	const int32 BindingCount = MS->GetPossessableCount() + MS->GetSpawnableCount();
	int32 Cleaned = 0;
	for (int32 i = 0; i < MS->GetPossessableCount(); ++i)
	{
		const FGuid Guid = MS->GetPossessable(i).GetGuid();
		FMovieSceneBindingProxy Proxy(Guid, Seq);
		Sub->RemoveInvalidBindings(Proxy);
		++Cleaned;
	}

	TSharedPtr<FJsonObject> R = MakeShared<FJsonObject>();
	R->SetBoolField(TEXT("success"), true);
	R->SetNumberField(TEXT("processed_bindings"), Cleaned);
	R->SetNumberField(TEXT("total_bindings"), BindingCount);
	OutJson = SerializeJson(R);
}

void HandleReplaceBindingWithActorsFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString SequencePath; Args->TryGetStringField(TEXT("sequence_path"), SequencePath);
	ULevelSequence* Seq = nullptr;
	if (!EnsureSequenceFocused(SequencePath, Seq, OutError)) return;
	UMovieScene* MS = Seq->GetMovieScene(); if (!MS) { OutError = TEXT("No MovieScene"); return; }

	const FGuid Guid = ResolveBindingGuid(MS, Args, OutError);
	if (!Guid.IsValid()) return;

	const TArray<TSharedPtr<FJsonValue>>* Labels = nullptr;
	if (!Args->TryGetArrayField(TEXT("actor_labels"), Labels) || !Labels)
	{
		OutError = TEXT("actor_labels (array) required");
		return;
	}

	TArray<AActor*> Actors;
	TArray<FString> Missing;
	for (const TSharedPtr<FJsonValue>& V : *Labels)
	{
		const FString L = V->AsString();
		if (AActor* A = FindActorByLabel(L)) Actors.Add(A);
		else Missing.Add(L);
	}

	ULevelSequenceEditorSubsystem* Sub = GetEditorSubsystem(OutError); if (!Sub) return;
	FMovieSceneBindingProxy Proxy(Guid, Seq);
	Sub->ReplaceBindingWithActors(Actors, Proxy);

	TSharedPtr<FJsonObject> R = MakeShared<FJsonObject>();
	R->SetBoolField(TEXT("success"), Missing.Num() == 0);
	R->SetNumberField(TEXT("bound_actor_count"), Actors.Num());
	if (Missing.Num() > 0)
	{
		TArray<TSharedPtr<FJsonValue>> MissArr;
		for (const FString& M : Missing) MissArr.Add(MakeShared<FJsonValueString>(M));
		R->SetArrayField(TEXT("missing_actors"), MissArr);
	}
	OutJson = SerializeJson(R);
}

void HandleAddActorsToBindingFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString SequencePath; Args->TryGetStringField(TEXT("sequence_path"), SequencePath);
	ULevelSequence* Seq = nullptr;
	if (!EnsureSequenceFocused(SequencePath, Seq, OutError)) return;
	UMovieScene* MS = Seq->GetMovieScene(); if (!MS) { OutError = TEXT("No MovieScene"); return; }

	const FGuid Guid = ResolveBindingGuid(MS, Args, OutError);
	if (!Guid.IsValid()) return;

	const TArray<TSharedPtr<FJsonValue>>* Labels = nullptr;
	if (!Args->TryGetArrayField(TEXT("actor_labels"), Labels) || !Labels)
	{
		OutError = TEXT("actor_labels (array) required");
		return;
	}

	TArray<AActor*> Actors;
	TArray<FString> Missing;
	for (const TSharedPtr<FJsonValue>& V : *Labels)
	{
		const FString L = V->AsString();
		if (AActor* A = FindActorByLabel(L)) Actors.Add(A);
		else Missing.Add(L);
	}

	ULevelSequenceEditorSubsystem* Sub = GetEditorSubsystem(OutError); if (!Sub) return;
	FMovieSceneBindingProxy Proxy(Guid, Seq);
	Sub->AddActorsToBinding(Actors, Proxy);

	TSharedPtr<FJsonObject> R = MakeShared<FJsonObject>();
	R->SetBoolField(TEXT("success"), Missing.Num() == 0);
	R->SetNumberField(TEXT("added_actor_count"), Actors.Num());
	OutJson = SerializeJson(R);
}

void HandleRemoveActorsFromBindingFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString SequencePath; Args->TryGetStringField(TEXT("sequence_path"), SequencePath);
	ULevelSequence* Seq = nullptr;
	if (!EnsureSequenceFocused(SequencePath, Seq, OutError)) return;
	UMovieScene* MS = Seq->GetMovieScene(); if (!MS) { OutError = TEXT("No MovieScene"); return; }

	const FGuid Guid = ResolveBindingGuid(MS, Args, OutError);
	if (!Guid.IsValid()) return;

	const TArray<TSharedPtr<FJsonValue>>* Labels = nullptr;
	if (!Args->TryGetArrayField(TEXT("actor_labels"), Labels) || !Labels)
	{
		OutError = TEXT("actor_labels (array) required");
		return;
	}

	TArray<AActor*> Actors;
	for (const TSharedPtr<FJsonValue>& V : *Labels)
	{
		if (AActor* A = FindActorByLabel(V->AsString())) Actors.Add(A);
	}

	ULevelSequenceEditorSubsystem* Sub = GetEditorSubsystem(OutError); if (!Sub) return;
	FMovieSceneBindingProxy Proxy(Guid, Seq);
	Sub->RemoveActorsFromBinding(Actors, Proxy);

	TSharedPtr<FJsonObject> R = MakeShared<FJsonObject>();
	R->SetBoolField(TEXT("success"), true);
	R->SetNumberField(TEXT("removed_actor_count"), Actors.Num());
	OutJson = SerializeJson(R);
}

void HandleRemoveAllBindingsFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString SequencePath; Args->TryGetStringField(TEXT("sequence_path"), SequencePath);
	ULevelSequence* Seq = nullptr;
	if (!EnsureSequenceFocused(SequencePath, Seq, OutError)) return;
	UMovieScene* MS = Seq->GetMovieScene(); if (!MS) { OutError = TEXT("No MovieScene"); return; }

	ULevelSequenceEditorSubsystem* Sub = GetEditorSubsystem(OutError); if (!Sub) return;

	int32 Cleared = 0;
	TArray<FGuid> Guids;
	for (int32 i = 0; i < MS->GetPossessableCount(); ++i) Guids.Add(MS->GetPossessable(i).GetGuid());
	for (int32 i = 0; i < MS->GetSpawnableCount(); ++i) Guids.Add(MS->GetSpawnable(i).GetGuid());
	for (const FGuid& G : Guids)
	{
		FMovieSceneBindingProxy Proxy(G, Seq);
		Sub->RemoveAllBindings(Proxy);
		++Cleared;
	}

	TSharedPtr<FJsonObject> R = MakeShared<FJsonObject>();
	R->SetBoolField(TEXT("success"), true);
	R->SetNumberField(TEXT("cleared_bindings"), Cleared);
	OutJson = SerializeJson(R);
}

void HandleConvertToSpawnableFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString SequencePath; Args->TryGetStringField(TEXT("sequence_path"), SequencePath);
	ULevelSequence* Seq = nullptr;
	if (!EnsureSequenceFocused(SequencePath, Seq, OutError)) return;
	UMovieScene* MS = Seq->GetMovieScene(); if (!MS) { OutError = TEXT("No MovieScene"); return; }

	const FGuid Guid = ResolveBindingGuid(MS, Args, OutError);
	if (!Guid.IsValid()) return;
ULevelSequenceEditorSubsystem* Sub = GetEditorSubsystem(OutError); if (!Sub) return;
	FMovieSceneBindingProxy Proxy(Guid, Seq);
	const TArray<FMovieSceneBindingProxy> Created = Sub->ConvertToSpawnable(Proxy);

	TArray<TSharedPtr<FJsonValue>> Arr;
	for (const FMovieSceneBindingProxy& P : Created)
	{
		TSharedPtr<FJsonObject> E = MakeShared<FJsonObject>();
		E->SetStringField(TEXT("guid"), P.BindingID.ToString(EGuidFormats::DigitsWithHyphens));
		E->SetStringField(TEXT("binding_label"), GetBindingLabelByGuid(MS, P.BindingID));
		Arr.Add(MakeShared<FJsonValueObject>(E));
	}

	TSharedPtr<FJsonObject> R = MakeShared<FJsonObject>();
	R->SetBoolField(TEXT("success"), true);
	R->SetArrayField(TEXT("spawnables"), Arr);
	OutJson = SerializeJson(R);
}

void HandleConvertToPossessableFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString SequencePath; Args->TryGetStringField(TEXT("sequence_path"), SequencePath);
	ULevelSequence* Seq = nullptr;
	if (!EnsureSequenceFocused(SequencePath, Seq, OutError)) return;
	UMovieScene* MS = Seq->GetMovieScene(); if (!MS) { OutError = TEXT("No MovieScene"); return; }

	const FGuid Guid = ResolveBindingGuid(MS, Args, OutError);
	if (!Guid.IsValid()) return;
ULevelSequenceEditorSubsystem* Sub = GetEditorSubsystem(OutError); if (!Sub) return;
	FMovieSceneBindingProxy Proxy(Guid, Seq);
	const FMovieSceneBindingProxy NewProxy = Sub->ConvertToPossessable(Proxy);

	TSharedPtr<FJsonObject> R = MakeShared<FJsonObject>();
	R->SetBoolField(TEXT("success"), true);
	R->SetStringField(TEXT("guid"), NewProxy.BindingID.ToString(EGuidFormats::DigitsWithHyphens));
	R->SetStringField(TEXT("binding_label"), GetBindingLabelByGuid(MS, NewProxy.BindingID));
	OutJson = SerializeJson(R);
}

void HandleChangeActorTemplateClassFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError)
{
#if !UE_VERSION_OLDER_THAN(5, 5, 0)
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString SequencePath; Args->TryGetStringField(TEXT("sequence_path"), SequencePath);
	ULevelSequence* Seq = nullptr;
	if (!EnsureSequenceFocused(SequencePath, Seq, OutError)) return;
	UMovieScene* MS = Seq->GetMovieScene(); if (!MS) { OutError = TEXT("No MovieScene"); return; }

	const FGuid Guid = ResolveBindingGuid(MS, Args, OutError);
	if (!Guid.IsValid()) return;
	FString NewClassPath; Args->TryGetStringField(TEXT("new_class"), NewClassPath);
	UClass* NewClass = ResolveActorClass(NewClassPath);
	if (!NewClass || !NewClass->IsChildOf(AActor::StaticClass()))
	{
		OutError = FString::Printf(TEXT("new_class '%s' did not resolve to an AActor subclass"), *NewClassPath);
		return;
	}

	ULevelSequenceEditorSubsystem* Sub = GetEditorSubsystem(OutError); if (!Sub) return;
	FMovieSceneBindingProxy Proxy(Guid, Seq);
	const bool bOk = Sub->ChangeActorTemplateClass(Proxy, TSubclassOf<AActor>(NewClass));

	TSharedPtr<FJsonObject> R = MakeShared<FJsonObject>();
	R->SetBoolField(TEXT("success"), bOk);
	OutJson = SerializeJson(R);
#else
	OutError = TEXT("change_actor_template_class requires UE 5.5+ (ULevelSequenceEditorSubsystem::ChangeActorTemplateClass was added in 5.5).");
#endif
}

void HandleSaveDefaultSpawnableStateFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError)
{
#if !UE_VERSION_OLDER_THAN(5, 5, 0)
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString SequencePath; Args->TryGetStringField(TEXT("sequence_path"), SequencePath);
	ULevelSequence* Seq = nullptr;
	if (!EnsureSequenceFocused(SequencePath, Seq, OutError)) return;
	UMovieScene* MS = Seq->GetMovieScene(); if (!MS) { OutError = TEXT("No MovieScene"); return; }

	const FGuid Guid = ResolveBindingGuid(MS, Args, OutError);
	if (!Guid.IsValid()) return;
ULevelSequenceEditorSubsystem* Sub = GetEditorSubsystem(OutError); if (!Sub) return;
	FMovieSceneBindingProxy Proxy(Guid, Seq);
	Sub->SaveDefaultSpawnableState(Proxy);

	TSharedPtr<FJsonObject> R = MakeShared<FJsonObject>();
	R->SetBoolField(TEXT("success"), true);
	OutJson = SerializeJson(R);
#else
	OutError = TEXT("save_default_spawnable_state requires UE 5.5+ (ULevelSequenceEditorSubsystem::SaveDefaultSpawnableState was added in 5.5).");
#endif
}

void HandleGetCustomBindingTypeFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString SequencePath; Args->TryGetStringField(TEXT("sequence_path"), SequencePath);
	ULevelSequence* Seq = nullptr;
	if (!EnsureSequenceFocused(SequencePath, Seq, OutError)) return;
	UMovieScene* MS = Seq->GetMovieScene(); if (!MS) { OutError = TEXT("No MovieScene"); return; }

	const FGuid Guid = ResolveBindingGuid(MS, Args, OutError);
	if (!Guid.IsValid()) return;

	FString Kind = TEXT("unknown");
	for (int32 i = 0; i < MS->GetPossessableCount(); ++i)
	{
		if (MS->GetPossessable(i).GetGuid() == Guid) { Kind = TEXT("possessable"); break; }
	}
	if (Kind == TEXT("unknown"))
	{
		for (int32 i = 0; i < MS->GetSpawnableCount(); ++i)
		{
			if (MS->GetSpawnable(i).GetGuid() == Guid) { Kind = TEXT("spawnable"); break; }
		}
	}

	FString CustomTypeName;
#if !UE_VERSION_OLDER_THAN(5, 5, 0)
	if (ULevelSequenceEditorSubsystem* Sub = GEditor ? GEditor->GetEditorSubsystem<ULevelSequenceEditorSubsystem>() : nullptr)
	{
		FMovieSceneBindingProxy Proxy(Guid, Seq);
		const TSubclassOf<UMovieSceneCustomBinding> CT = Sub->GetCustomBindingType(Proxy);
		if (UClass* Cls = CT.Get()) CustomTypeName = Cls->GetName();
	}
#endif

	if (!CustomTypeName.IsEmpty())
	{
		if (CustomTypeName.Contains(TEXT("Spawnable")))   Kind = TEXT("spawnable");
		else if (CustomTypeName.Contains(TEXT("Replaceable"))) Kind = TEXT("replaceable");
	}

	TSharedPtr<FJsonObject> R = MakeShared<FJsonObject>();
	R->SetBoolField(TEXT("success"), true);
	R->SetStringField(TEXT("kind"), Kind);
	if (!CustomTypeName.IsEmpty()) R->SetStringField(TEXT("custom_binding_type"), CustomTypeName);
	OutJson = SerializeJson(R);
}

}
