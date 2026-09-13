// Copyright 2026, BlueprintsLab, All rights reserved.

#include "Tools/PhysicsAssetTools.h"
#include "Tools/BatchToolHelper.h"

#include "PhysicsEngine/PhysicsAsset.h"
#include "Misc/EngineVersionComparison.h"
#if UE_VERSION_OLDER_THAN(5, 5, 0)
#include "PhysicsEngine/PhysicsAsset.h"
#else
#include "PhysicsEngine/SkeletalBodySetup.h"
#endif
#include "PhysicsEngine/PhysicsConstraintTemplate.h"
#include "PhysicsEngine/AggregateGeom.h"
#include "PhysicsEngine/ConstraintInstance.h"

#include "EditorAssetLibrary.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace PhysicsAssetTools
{

namespace
{
	UPhysicsAsset* LoadAsset(const FString& Path, FString& OutError)
	{
		UPhysicsAsset* Asset = Cast<UPhysicsAsset>(UEditorAssetLibrary::LoadAsset(Path));
		if (!Asset) OutError = FString::Printf(TEXT("PhysicsAsset not found at '%s'"), *Path);
		return Asset;
	}

	USkeletalBodySetup* FindBody(UPhysicsAsset* Asset, const FString& BoneName)
	{
		const FName BoneFName(*BoneName);
		for (USkeletalBodySetup* BS : Asset->SkeletalBodySetups)
		{
			if (BS && BS->BoneName == BoneFName) return BS;
		}
		return nullptr;
	}

	void NotifyAssetChanged(UPhysicsAsset* Asset)
	{
		Asset->UpdateBodySetupIndexMap();
		Asset->UpdateBoundsBodiesArray();
		Asset->RefreshPhysicsAssetChange();
		Asset->MarkPackageDirty();
	}

	UPhysicsConstraintTemplate* FindConstraint(UPhysicsAsset* Asset, const FString& Bone1, const FString& Bone2)
	{
		const FName B1(*Bone1);
		const FName B2(*Bone2);
		for (UPhysicsConstraintTemplate* CT : Asset->ConstraintSetup)
		{
			if (!CT) continue;
			const FName C1 = CT->DefaultInstance.ConstraintBone1;
			const FName C2 = CT->DefaultInstance.ConstraintBone2;
			if ((C1 == B1 && C2 == B2) || (C1 == B2 && C2 == B1)) return CT;
		}
		return nullptr;
	}

	FString SerializeJson(const TSharedPtr<FJsonObject>& Obj)
	{
		FString Out;
		const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Out);
		FJsonSerializer::Serialize(Obj.ToSharedRef(), Writer);
		return Out;
	}

	bool ReadVector(const TSharedPtr<FJsonObject>& Source, const FString& Field, FVector& Out)
	{
		const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
		if (!Source->TryGetArrayField(Field, Arr) || !Arr || Arr->Num() < 3) return false;
		Out.X = (*Arr)[0]->AsNumber();
		Out.Y = (*Arr)[1]->AsNumber();
		Out.Z = (*Arr)[2]->AsNumber();
		return true;
	}

	bool ReadRotator(const TSharedPtr<FJsonObject>& Source, const FString& Field, FRotator& Out)
	{
		const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
		if (!Source->TryGetArrayField(Field, Arr) || !Arr || Arr->Num() < 3) return false;
		Out.Pitch = (*Arr)[0]->AsNumber();
		Out.Yaw   = (*Arr)[1]->AsNumber();
		Out.Roll  = (*Arr)[2]->AsNumber();
		return true;
	}

	void RemoveShapeByName(USkeletalBodySetup* Body, const FName& ShapeName)
	{
		FKAggregateGeom& Geom = Body->AggGeom;
		Geom.SphereElems.RemoveAll([&](const FKSphereElem& E) { return E.GetName() == ShapeName; });
		Geom.SphylElems .RemoveAll([&](const FKSphylElem& E)  { return E.GetName() == ShapeName; });
		Geom.BoxElems   .RemoveAll([&](const FKBoxElem& E)    { return E.GetName() == ShapeName; });
		Geom.ConvexElems.RemoveAll([&](const FKConvexElem& E) { return E.GetName() == ShapeName; });
	}

	EPhysicsType ParsePhysicsMode(const FString& Mode, bool& bOk)
	{
		bOk = true;
		if (Mode.Equals(TEXT("default"),   ESearchCase::IgnoreCase)) return EPhysicsType::PhysType_Default;
		if (Mode.Equals(TEXT("kinematic"), ESearchCase::IgnoreCase)) return EPhysicsType::PhysType_Kinematic;
		if (Mode.Equals(TEXT("simulated"), ESearchCase::IgnoreCase)) return EPhysicsType::PhysType_Simulated;
		bOk = false;
		return EPhysicsType::PhysType_Default;
	}

	const TCHAR* PhysicsModeToString(EPhysicsType Mode)
	{
		switch (Mode)
		{
			case EPhysicsType::PhysType_Kinematic: return TEXT("kinematic");
			case EPhysicsType::PhysType_Simulated: return TEXT("simulated");
			default: return TEXT("default");
		}
	}

	EAngularConstraintMotion ParseAngularMotion(const FString& Motion, bool& bOk)
	{
		bOk = true;
		if (Motion.Equals(TEXT("free"),    ESearchCase::IgnoreCase)) return EAngularConstraintMotion::ACM_Free;
		if (Motion.Equals(TEXT("limited"), ESearchCase::IgnoreCase)) return EAngularConstraintMotion::ACM_Limited;
		if (Motion.Equals(TEXT("locked"),  ESearchCase::IgnoreCase)) return EAngularConstraintMotion::ACM_Locked;
		bOk = false;
		return EAngularConstraintMotion::ACM_Free;
	}

	const TCHAR* AngularMotionToString(EAngularConstraintMotion Motion)
	{
		switch (Motion)
		{
			case EAngularConstraintMotion::ACM_Limited: return TEXT("limited");
			case EAngularConstraintMotion::ACM_Locked:  return TEXT("locked");
			default: return TEXT("free");
		}
	}
}

void HandleAddBodyFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString Path; Args->TryGetStringField(TEXT("physics_asset_path"), Path);
	UPhysicsAsset* Asset = LoadAsset(Path, OutError); if (!Asset) return;

	auto AddOne = [&](const FString& BoneName, FString& OutErr) -> bool
	{
		if (BoneName.IsEmpty()) { OutErr = TEXT("bone_name required"); return false; }
		if (FindBody(Asset, BoneName))
		{
			OutErr = FString::Printf(TEXT("Body for bone '%s' already exists"), *BoneName);
			return false;
		}
		USkeletalBodySetup* BS = NewObject<USkeletalBodySetup>(Asset, NAME_None, RF_Transactional);
		BS->BoneName = FName(*BoneName);
		Asset->Modify();
		Asset->SkeletalBodySetups.Add(BS);
		return true;
	};

	const TArray<TSharedPtr<FJsonValue>>* Items = nullptr;
	if (BatchToolHelper::TryGetBatchItems(Args, TEXT("items"), Items))
	{
		BatchToolHelper::FBatchResultBuilder Batch;
		for (int32 i = 0; i < Items->Num(); ++i)
		{
			const TSharedPtr<FJsonObject> Entry = (*Items)[i]->AsObject();
			if (!Entry.IsValid()) { Batch.AddFailure(i, TEXT("Invalid item")); continue; }
			FString Bone; Entry->TryGetStringField(TEXT("bone_name"), Bone);
			FString Err;
			if (!AddOne(Bone, Err)) { Batch.AddFailure(i, Err); continue; }
			TSharedPtr<FJsonObject> R = MakeShared<FJsonObject>();
			R->SetStringField(TEXT("bone_name"), Bone);
			Batch.AddSuccess(i, R);
		}
		NotifyAssetChanged(Asset);
		Batch.Finalize(OutJson);
		return;
	}

	FString Bone; Args->TryGetStringField(TEXT("bone_name"), Bone);
	if (!AddOne(Bone, OutError)) return;
	NotifyAssetChanged(Asset);

	TSharedPtr<FJsonObject> R = MakeShared<FJsonObject>();
	R->SetBoolField(TEXT("success"), true);
	R->SetStringField(TEXT("bone_name"), Bone);
	OutJson = SerializeJson(R);
}

void HandleRemoveBodyFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString Path; Args->TryGetStringField(TEXT("physics_asset_path"), Path);
	UPhysicsAsset* Asset = LoadAsset(Path, OutError); if (!Asset) return;

	auto RemoveOne = [&](const FString& BoneName, FString& OutErr, int32& OutRemovedConstraints) -> bool
	{
		const FName BoneFName(*BoneName);
		const int32 BodyIdx = Asset->SkeletalBodySetups.IndexOfByPredicate([&](USkeletalBodySetup* BS)
		{
			return BS && BS->BoneName == BoneFName;
		});
		if (BodyIdx == INDEX_NONE)
		{
			OutErr = FString::Printf(TEXT("No body found for bone '%s'"), *BoneName);
			return false;
		}
		Asset->Modify();
		OutRemovedConstraints = Asset->ConstraintSetup.RemoveAll([&](UPhysicsConstraintTemplate* CT)
		{
			if (!CT) return true;
			return CT->DefaultInstance.ConstraintBone1 == BoneFName
				|| CT->DefaultInstance.ConstraintBone2 == BoneFName;
		});
		Asset->SkeletalBodySetups.RemoveAt(BodyIdx);
		return true;
	};

	const TArray<TSharedPtr<FJsonValue>>* Items = nullptr;
	if (BatchToolHelper::TryGetBatchItems(Args, TEXT("items"), Items))
	{
		BatchToolHelper::FBatchResultBuilder Batch;
		for (int32 i = 0; i < Items->Num(); ++i)
		{
			const TSharedPtr<FJsonObject> Entry = (*Items)[i]->AsObject();
			if (!Entry.IsValid()) { Batch.AddFailure(i, TEXT("Invalid item")); continue; }
			FString Bone; Entry->TryGetStringField(TEXT("bone_name"), Bone);
			int32 Removed = 0; FString Err;
			if (!RemoveOne(Bone, Err, Removed)) { Batch.AddFailure(i, Err); continue; }
			TSharedPtr<FJsonObject> R = MakeShared<FJsonObject>();
			R->SetStringField(TEXT("bone_name"), Bone);
			R->SetNumberField(TEXT("removed_constraints"), Removed);
			Batch.AddSuccess(i, R);
		}
		NotifyAssetChanged(Asset);
		Batch.Finalize(OutJson);
		return;
	}

	FString Bone; Args->TryGetStringField(TEXT("bone_name"), Bone);
	int32 Removed = 0;
	if (!RemoveOne(Bone, OutError, Removed)) return;
	NotifyAssetChanged(Asset);

	TSharedPtr<FJsonObject> R = MakeShared<FJsonObject>();
	R->SetBoolField(TEXT("success"), true);
	R->SetStringField(TEXT("bone_name"), Bone);
	R->SetNumberField(TEXT("removed_constraints"), Removed);
	OutJson = SerializeJson(R);
}

void HandleGetBodyNamesFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString Path; Args->TryGetStringField(TEXT("physics_asset_path"), Path);
	UPhysicsAsset* Asset = LoadAsset(Path, OutError); if (!Asset) return;

	TArray<TSharedPtr<FJsonValue>> Arr;
	for (const USkeletalBodySetup* BS : Asset->SkeletalBodySetups)
	{
		if (BS) Arr.Add(MakeShared<FJsonValueString>(BS->BoneName.ToString()));
	}

	TSharedPtr<FJsonObject> R = MakeShared<FJsonObject>();
	R->SetBoolField(TEXT("success"), true);
	R->SetNumberField(TEXT("count"), Arr.Num());
	R->SetArrayField(TEXT("bones"), Arr);
	OutJson = SerializeJson(R);
}

void HandleGetBodyShapesFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString Path, Bone;
	Args->TryGetStringField(TEXT("physics_asset_path"), Path);
	Args->TryGetStringField(TEXT("bone_name"), Bone);
	UPhysicsAsset* Asset = LoadAsset(Path, OutError); if (!Asset) return;
	USkeletalBodySetup* BS = FindBody(Asset, Bone);
	if (!BS) { OutError = FString::Printf(TEXT("No body found for bone '%s'"), *Bone); return; }

	auto VectorJson = [](const FVector& V)
	{
		TArray<TSharedPtr<FJsonValue>> Arr;
		Arr.Add(MakeShared<FJsonValueNumber>(V.X));
		Arr.Add(MakeShared<FJsonValueNumber>(V.Y));
		Arr.Add(MakeShared<FJsonValueNumber>(V.Z));
		return Arr;
	};
	auto RotatorJson = [](const FRotator& R)
	{
		TArray<TSharedPtr<FJsonValue>> Arr;
		Arr.Add(MakeShared<FJsonValueNumber>(R.Pitch));
		Arr.Add(MakeShared<FJsonValueNumber>(R.Yaw));
		Arr.Add(MakeShared<FJsonValueNumber>(R.Roll));
		return Arr;
	};

	TArray<TSharedPtr<FJsonValue>> Shapes;
	for (const FKSphereElem& E : BS->AggGeom.SphereElems)
	{
		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("shape_type"), TEXT("sphere"));
		Entry->SetStringField(TEXT("shape_name"), E.GetName().ToString());
		Entry->SetArrayField(TEXT("center"), VectorJson(E.Center));
		Entry->SetNumberField(TEXT("radius"), E.Radius);
		Shapes.Add(MakeShared<FJsonValueObject>(Entry));
	}
	for (const FKSphylElem& E : BS->AggGeom.SphylElems)
	{
		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("shape_type"), TEXT("capsule"));
		Entry->SetStringField(TEXT("shape_name"), E.GetName().ToString());
		Entry->SetArrayField(TEXT("center"), VectorJson(E.Center));
		Entry->SetArrayField(TEXT("rotation"), RotatorJson(E.Rotation));
		Entry->SetNumberField(TEXT("radius"), E.Radius);
		Entry->SetNumberField(TEXT("length"), E.Length);
		Shapes.Add(MakeShared<FJsonValueObject>(Entry));
	}
	for (const FKBoxElem& E : BS->AggGeom.BoxElems)
	{
		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("shape_type"), TEXT("box"));
		Entry->SetStringField(TEXT("shape_name"), E.GetName().ToString());
		Entry->SetArrayField(TEXT("center"), VectorJson(E.Center));
		Entry->SetArrayField(TEXT("rotation"), RotatorJson(E.Rotation));
		TArray<TSharedPtr<FJsonValue>> Extent;
		Extent.Add(MakeShared<FJsonValueNumber>(E.X));
		Extent.Add(MakeShared<FJsonValueNumber>(E.Y));
		Extent.Add(MakeShared<FJsonValueNumber>(E.Z));
		Entry->SetArrayField(TEXT("extent"), Extent);
		Shapes.Add(MakeShared<FJsonValueObject>(Entry));
	}

	TSharedPtr<FJsonObject> R = MakeShared<FJsonObject>();
	R->SetBoolField(TEXT("success"), true);
	R->SetStringField(TEXT("bone_name"), Bone);
	R->SetNumberField(TEXT("count"), Shapes.Num());
	R->SetArrayField(TEXT("shapes"), Shapes);
	OutJson = SerializeJson(R);
}

namespace
{
	bool ApplySphere(USkeletalBodySetup* BS, const FString& ShapeName, const FVector& Center, float Radius, FString& OutErr)
	{
		if (Radius <= 0.f) { OutErr = TEXT("radius must be > 0"); return false; }
		RemoveShapeByName(BS, FName(*ShapeName));
		FKSphereElem Elem(Radius);
		Elem.SetName(FName(*ShapeName));
		Elem.Center = Center;
		BS->AggGeom.SphereElems.Add(Elem);
		return true;
	}

	bool ApplyCapsule(USkeletalBodySetup* BS, const FString& ShapeName, const FVector& Center, const FRotator& Rotation, float Radius, float Length, FString& OutErr)
	{
		if (Radius <= 0.f) { OutErr = TEXT("radius must be > 0"); return false; }
		if (Length < 0.f)  { OutErr = TEXT("length must be >= 0"); return false; }
		RemoveShapeByName(BS, FName(*ShapeName));
		FKSphylElem Elem(Radius, Length);
		Elem.SetName(FName(*ShapeName));
		Elem.Center = Center;
		Elem.Rotation = Rotation;
		BS->AggGeom.SphylElems.Add(Elem);
		return true;
	}

	bool ApplyBox(USkeletalBodySetup* BS, const FString& ShapeName, const FVector& Center, const FRotator& Rotation, const FVector& Extent, FString& OutErr)
	{
		if (Extent.X <= 0.f || Extent.Y <= 0.f || Extent.Z <= 0.f)
		{
			OutErr = TEXT("extent components must all be > 0");
			return false;
		}
		RemoveShapeByName(BS, FName(*ShapeName));
		FKBoxElem Elem(Extent.X, Extent.Y, Extent.Z);
		Elem.SetName(FName(*ShapeName));
		Elem.Center = Center;
		Elem.Rotation = Rotation;
		BS->AggGeom.BoxElems.Add(Elem);
		return true;
	}

	template <typename ApplyFn>
	void RunShapeOp(const TSharedPtr<FJsonObject>& Args, ApplyFn Apply,
		FString& OutJson, FString& OutError)
	{
		FString Path; Args->TryGetStringField(TEXT("physics_asset_path"), Path);
		UPhysicsAsset* Asset = LoadAsset(Path, OutError); if (!Asset) return;

		const TArray<TSharedPtr<FJsonValue>>* Items = nullptr;
		if (BatchToolHelper::TryGetBatchItems(Args, TEXT("items"), Items))
		{
			BatchToolHelper::FBatchResultBuilder Batch;
			for (int32 i = 0; i < Items->Num(); ++i)
			{
				const TSharedPtr<FJsonObject> Entry = (*Items)[i]->AsObject();
				if (!Entry.IsValid()) { Batch.AddFailure(i, TEXT("Invalid item")); continue; }
				FString Bone, ShapeName;
				Entry->TryGetStringField(TEXT("bone_name"), Bone);
				Entry->TryGetStringField(TEXT("shape_name"), ShapeName);
				USkeletalBodySetup* BS = FindBody(Asset, Bone);
				if (!BS) { Batch.AddFailure(i, FString::Printf(TEXT("No body for bone '%s'"), *Bone)); continue; }
				if (ShapeName.IsEmpty()) { Batch.AddFailure(i, TEXT("shape_name required")); continue; }
				BS->Modify();
				FString Err;
				if (!Apply(BS, Entry, ShapeName, Err)) { Batch.AddFailure(i, Err); continue; }
				TSharedPtr<FJsonObject> R = MakeShared<FJsonObject>();
				R->SetStringField(TEXT("bone_name"), Bone);
				R->SetStringField(TEXT("shape_name"), ShapeName);
				Batch.AddSuccess(i, R);
			}
			Asset->MarkPackageDirty();
			Batch.Finalize(OutJson);
			return;
		}

		FString Bone, ShapeName;
		Args->TryGetStringField(TEXT("bone_name"), Bone);
		Args->TryGetStringField(TEXT("shape_name"), ShapeName);
		USkeletalBodySetup* BS = FindBody(Asset, Bone);
		if (!BS) { OutError = FString::Printf(TEXT("No body for bone '%s'"), *Bone); return; }
		if (ShapeName.IsEmpty()) { OutError = TEXT("shape_name required"); return; }
		BS->Modify();
		if (!Apply(BS, Args, ShapeName, OutError)) return;
		Asset->MarkPackageDirty();

		TSharedPtr<FJsonObject> R = MakeShared<FJsonObject>();
		R->SetBoolField(TEXT("success"), true);
		R->SetStringField(TEXT("bone_name"), Bone);
		R->SetStringField(TEXT("shape_name"), ShapeName);
		OutJson = SerializeJson(R);
	}
}

void HandleSetSphereFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	RunShapeOp(Args, [](USkeletalBodySetup* BS, const TSharedPtr<FJsonObject>& Source, const FString& Name, FString& Err)
	{
		FVector Center = FVector::ZeroVector; ReadVector(Source, TEXT("center"), Center);
		double Radius = 0.0; Source->TryGetNumberField(TEXT("radius"), Radius);
		return ApplySphere(BS, Name, Center, static_cast<float>(Radius), Err);
	}, OutJson, OutError);
}

void HandleSetCapsuleFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	RunShapeOp(Args, [](USkeletalBodySetup* BS, const TSharedPtr<FJsonObject>& Source, const FString& Name, FString& Err)
	{
		FVector Center = FVector::ZeroVector; ReadVector(Source, TEXT("center"), Center);
		FRotator Rot = FRotator::ZeroRotator; ReadRotator(Source, TEXT("rotation"), Rot);
		double Radius = 0.0; Source->TryGetNumberField(TEXT("radius"), Radius);
		double Length = 0.0; Source->TryGetNumberField(TEXT("length"), Length);
		return ApplyCapsule(BS, Name, Center, Rot, static_cast<float>(Radius), static_cast<float>(Length), Err);
	}, OutJson, OutError);
}

void HandleSetBoxFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	RunShapeOp(Args, [](USkeletalBodySetup* BS, const TSharedPtr<FJsonObject>& Source, const FString& Name, FString& Err)
	{
		FVector Center = FVector::ZeroVector; ReadVector(Source, TEXT("center"), Center);
		FRotator Rot = FRotator::ZeroRotator; ReadRotator(Source, TEXT("rotation"), Rot);
		FVector Extent = FVector::ZeroVector; ReadVector(Source, TEXT("extent"), Extent);
		return ApplyBox(BS, Name, Center, Rot, Extent, Err);
	}, OutJson, OutError);
}

void HandleRemoveShapeFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString Path; Args->TryGetStringField(TEXT("physics_asset_path"), Path);
	UPhysicsAsset* Asset = LoadAsset(Path, OutError); if (!Asset) return;

	auto RemoveOne = [&](const FString& Bone, const FString& ShapeName, FString& OutErr) -> bool
	{
		USkeletalBodySetup* BS = FindBody(Asset, Bone);
		if (!BS) { OutErr = FString::Printf(TEXT("No body for bone '%s'"), *Bone); return false; }
		BS->Modify();
		RemoveShapeByName(BS, FName(*ShapeName));
		return true;
	};

	const TArray<TSharedPtr<FJsonValue>>* Items = nullptr;
	if (BatchToolHelper::TryGetBatchItems(Args, TEXT("items"), Items))
	{
		BatchToolHelper::FBatchResultBuilder Batch;
		for (int32 i = 0; i < Items->Num(); ++i)
		{
			const TSharedPtr<FJsonObject> Entry = (*Items)[i]->AsObject();
			if (!Entry.IsValid()) { Batch.AddFailure(i, TEXT("Invalid item")); continue; }
			FString Bone, ShapeName;
			Entry->TryGetStringField(TEXT("bone_name"), Bone);
			Entry->TryGetStringField(TEXT("shape_name"), ShapeName);
			FString Err;
			if (!RemoveOne(Bone, ShapeName, Err)) { Batch.AddFailure(i, Err); continue; }
			TSharedPtr<FJsonObject> R = MakeShared<FJsonObject>();
			R->SetStringField(TEXT("bone_name"), Bone);
			R->SetStringField(TEXT("shape_name"), ShapeName);
			Batch.AddSuccess(i, R);
		}
		Asset->MarkPackageDirty();
		Batch.Finalize(OutJson);
		return;
	}

	FString Bone, ShapeName;
	Args->TryGetStringField(TEXT("bone_name"), Bone);
	Args->TryGetStringField(TEXT("shape_name"), ShapeName);
	if (!RemoveOne(Bone, ShapeName, OutError)) return;
	Asset->MarkPackageDirty();

	TSharedPtr<FJsonObject> R = MakeShared<FJsonObject>();
	R->SetBoolField(TEXT("success"), true);
	OutJson = SerializeJson(R);
}

void HandleSetBodyPhysicsModeFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString Path; Args->TryGetStringField(TEXT("physics_asset_path"), Path);
	UPhysicsAsset* Asset = LoadAsset(Path, OutError); if (!Asset) return;

	auto ApplyOne = [&](const FString& Bone, const FString& Mode, FString& OutErr) -> bool
	{
		USkeletalBodySetup* BS = FindBody(Asset, Bone);
		if (!BS) { OutErr = FString::Printf(TEXT("No body for bone '%s'"), *Bone); return false; }
		bool bOk = false;
		const EPhysicsType ParsedMode = ParsePhysicsMode(Mode, bOk);
		if (!bOk) { OutErr = FString::Printf(TEXT("Unknown mode '%s' (default|kinematic|simulated)"), *Mode); return false; }
		BS->Modify();
		BS->PhysicsType = ParsedMode;
		return true;
	};

	const TArray<TSharedPtr<FJsonValue>>* Items = nullptr;
	if (BatchToolHelper::TryGetBatchItems(Args, TEXT("items"), Items))
	{
		BatchToolHelper::FBatchResultBuilder Batch;
		for (int32 i = 0; i < Items->Num(); ++i)
		{
			const TSharedPtr<FJsonObject> Entry = (*Items)[i]->AsObject();
			if (!Entry.IsValid()) { Batch.AddFailure(i, TEXT("Invalid item")); continue; }
			FString Bone, Mode;
			Entry->TryGetStringField(TEXT("bone_name"), Bone);
			Entry->TryGetStringField(TEXT("mode"), Mode);
			FString Err;
			if (!ApplyOne(Bone, Mode, Err)) { Batch.AddFailure(i, Err); continue; }
			TSharedPtr<FJsonObject> R = MakeShared<FJsonObject>();
			R->SetStringField(TEXT("bone_name"), Bone);
			R->SetStringField(TEXT("mode"), Mode);
			Batch.AddSuccess(i, R);
		}
		Asset->MarkPackageDirty();
		Batch.Finalize(OutJson);
		return;
	}

	FString Bone, Mode;
	Args->TryGetStringField(TEXT("bone_name"), Bone);
	Args->TryGetStringField(TEXT("mode"), Mode);
	if (!ApplyOne(Bone, Mode, OutError)) return;
	Asset->MarkPackageDirty();

	TSharedPtr<FJsonObject> R = MakeShared<FJsonObject>();
	R->SetBoolField(TEXT("success"), true);
	R->SetStringField(TEXT("bone_name"), Bone);
	R->SetStringField(TEXT("mode"), Mode);
	OutJson = SerializeJson(R);
}

void HandleGetBodyPhysicsModeFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString Path, Bone;
	Args->TryGetStringField(TEXT("physics_asset_path"), Path);
	Args->TryGetStringField(TEXT("bone_name"), Bone);
	UPhysicsAsset* Asset = LoadAsset(Path, OutError); if (!Asset) return;
	USkeletalBodySetup* BS = FindBody(Asset, Bone);
	if (!BS) { OutError = FString::Printf(TEXT("No body for bone '%s'"), *Bone); return; }

	TSharedPtr<FJsonObject> R = MakeShared<FJsonObject>();
	R->SetBoolField(TEXT("success"), true);
	R->SetStringField(TEXT("bone_name"), Bone);
	R->SetStringField(TEXT("mode"), PhysicsModeToString(BS->PhysicsType));
	OutJson = SerializeJson(R);
}

void HandleSetBodyMassScaleFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString Path; Args->TryGetStringField(TEXT("physics_asset_path"), Path);
	UPhysicsAsset* Asset = LoadAsset(Path, OutError); if (!Asset) return;

	auto ApplyOne = [&](const FString& Bone, double Scale, FString& OutErr) -> bool
	{
		if (Scale <= 0.0) { OutErr = TEXT("mass_scale must be > 0"); return false; }
		USkeletalBodySetup* BS = FindBody(Asset, Bone);
		if (!BS) { OutErr = FString::Printf(TEXT("No body for bone '%s'"), *Bone); return false; }
		BS->Modify();
		BS->DefaultInstance.MassScale = static_cast<float>(Scale);
		return true;
	};

	const TArray<TSharedPtr<FJsonValue>>* Items = nullptr;
	if (BatchToolHelper::TryGetBatchItems(Args, TEXT("items"), Items))
	{
		BatchToolHelper::FBatchResultBuilder Batch;
		for (int32 i = 0; i < Items->Num(); ++i)
		{
			const TSharedPtr<FJsonObject> Entry = (*Items)[i]->AsObject();
			if (!Entry.IsValid()) { Batch.AddFailure(i, TEXT("Invalid item")); continue; }
			FString Bone; Entry->TryGetStringField(TEXT("bone_name"), Bone);
			double Scale = 0.0; Entry->TryGetNumberField(TEXT("mass_scale"), Scale);
			FString Err;
			if (!ApplyOne(Bone, Scale, Err)) { Batch.AddFailure(i, Err); continue; }
			TSharedPtr<FJsonObject> R = MakeShared<FJsonObject>();
			R->SetStringField(TEXT("bone_name"), Bone);
			R->SetNumberField(TEXT("mass_scale"), Scale);
			Batch.AddSuccess(i, R);
		}
		Asset->MarkPackageDirty();
		Batch.Finalize(OutJson);
		return;
	}

	FString Bone; Args->TryGetStringField(TEXT("bone_name"), Bone);
	double Scale = 0.0; Args->TryGetNumberField(TEXT("mass_scale"), Scale);
	if (!ApplyOne(Bone, Scale, OutError)) return;
	Asset->MarkPackageDirty();

	TSharedPtr<FJsonObject> R = MakeShared<FJsonObject>();
	R->SetBoolField(TEXT("success"), true);
	R->SetStringField(TEXT("bone_name"), Bone);
	R->SetNumberField(TEXT("mass_scale"), Scale);
	OutJson = SerializeJson(R);
}

void HandleGetBodyMassScaleFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString Path, Bone;
	Args->TryGetStringField(TEXT("physics_asset_path"), Path);
	Args->TryGetStringField(TEXT("bone_name"), Bone);
	UPhysicsAsset* Asset = LoadAsset(Path, OutError); if (!Asset) return;
	USkeletalBodySetup* BS = FindBody(Asset, Bone);
	if (!BS) { OutError = FString::Printf(TEXT("No body for bone '%s'"), *Bone); return; }

	TSharedPtr<FJsonObject> R = MakeShared<FJsonObject>();
	R->SetBoolField(TEXT("success"), true);
	R->SetStringField(TEXT("bone_name"), Bone);
	R->SetNumberField(TEXT("mass_scale"), BS->DefaultInstance.MassScale);
	OutJson = SerializeJson(R);
}

void HandleGetConstraintsFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString Path; Args->TryGetStringField(TEXT("physics_asset_path"), Path);
	UPhysicsAsset* Asset = LoadAsset(Path, OutError); if (!Asset) return;

	TArray<TSharedPtr<FJsonValue>> Arr;
	for (const UPhysicsConstraintTemplate* CT : Asset->ConstraintSetup)
	{
		if (!CT) continue;
		const FConstraintInstance& CI = CT->DefaultInstance;
		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("bone1"), CI.ConstraintBone1.ToString());
		Entry->SetStringField(TEXT("bone2"), CI.ConstraintBone2.ToString());
		Entry->SetStringField(TEXT("swing1_motion"), AngularMotionToString(CI.GetAngularSwing1Motion()));
		Entry->SetNumberField(TEXT("swing1_limit_deg"), CI.GetAngularSwing1Limit());
		Entry->SetStringField(TEXT("swing2_motion"), AngularMotionToString(CI.GetAngularSwing2Motion()));
		Entry->SetNumberField(TEXT("swing2_limit_deg"), CI.GetAngularSwing2Limit());
		Entry->SetStringField(TEXT("twist_motion"), AngularMotionToString(CI.GetAngularTwistMotion()));
		Entry->SetNumberField(TEXT("twist_limit_deg"), CI.GetAngularTwistLimit());
		Arr.Add(MakeShared<FJsonValueObject>(Entry));
	}

	TSharedPtr<FJsonObject> R = MakeShared<FJsonObject>();
	R->SetBoolField(TEXT("success"), true);
	R->SetNumberField(TEXT("count"), Arr.Num());
	R->SetArrayField(TEXT("constraints"), Arr);
	OutJson = SerializeJson(R);
}

void HandleSetConstraintLimitsFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString Path; Args->TryGetStringField(TEXT("physics_asset_path"), Path);
	UPhysicsAsset* Asset = LoadAsset(Path, OutError); if (!Asset) return;

	auto ApplyOne = [&](const TSharedPtr<FJsonObject>& Source, FString& OutErr) -> bool
	{
		FString B1, B2;
		Source->TryGetStringField(TEXT("bone1"), B1);
		Source->TryGetStringField(TEXT("bone2"), B2);
		UPhysicsConstraintTemplate* CT = FindConstraint(Asset, B1, B2);
		if (!CT) { OutErr = FString::Printf(TEXT("No constraint between '%s' and '%s'"), *B1, *B2); return false; }

		FConstraintInstance& CI = CT->DefaultInstance;
		bool bAnyApplied = false;

		auto ApplyAngular = [&](const FString& MotionField, const FString& LimitField,
			TFunctionRef<void(EAngularConstraintMotion, float)> Setter)
		{
			FString MotionStr;
			Source->TryGetStringField(MotionField, MotionStr);
			double Limit = 0.0;
			const bool bHasLimit = Source->TryGetNumberField(LimitField, Limit);

			if (MotionStr.IsEmpty() && !bHasLimit) return;

			bool bMotionOk = true;
			EAngularConstraintMotion Motion = ParseAngularMotion(MotionStr, bMotionOk);
			if (!bMotionOk) Motion = EAngularConstraintMotion::ACM_Limited;
			Setter(Motion, static_cast<float>(Limit));
			bAnyApplied = true;
		};

		CT->Modify();
		ApplyAngular(TEXT("swing1_motion"), TEXT("swing1_limit_deg"),
			[&](EAngularConstraintMotion M, float L) { CI.SetAngularSwing1Limit(M, L); });
		ApplyAngular(TEXT("swing2_motion"), TEXT("swing2_limit_deg"),
			[&](EAngularConstraintMotion M, float L) { CI.SetAngularSwing2Limit(M, L); });
		ApplyAngular(TEXT("twist_motion"), TEXT("twist_limit_deg"),
			[&](EAngularConstraintMotion M, float L) { CI.SetAngularTwistLimit(M, L); });

		if (!bAnyApplied) { OutErr = TEXT("at least one of swing1/swing2/twist motion or limit required"); return false; }
		return true;
	};

	const TArray<TSharedPtr<FJsonValue>>* Items = nullptr;
	if (BatchToolHelper::TryGetBatchItems(Args, TEXT("items"), Items))
	{
		BatchToolHelper::FBatchResultBuilder Batch;
		for (int32 i = 0; i < Items->Num(); ++i)
		{
			const TSharedPtr<FJsonObject> Entry = (*Items)[i]->AsObject();
			if (!Entry.IsValid()) { Batch.AddFailure(i, TEXT("Invalid item")); continue; }
			FString Err;
			if (!ApplyOne(Entry, Err)) { Batch.AddFailure(i, Err); continue; }
			FString B1, B2;
			Entry->TryGetStringField(TEXT("bone1"), B1);
			Entry->TryGetStringField(TEXT("bone2"), B2);
			TSharedPtr<FJsonObject> R = MakeShared<FJsonObject>();
			R->SetStringField(TEXT("bone1"), B1);
			R->SetStringField(TEXT("bone2"), B2);
			Batch.AddSuccess(i, R);
		}
		Asset->MarkPackageDirty();
		Batch.Finalize(OutJson);
		return;
	}

	if (!ApplyOne(Args, OutError)) return;
	Asset->MarkPackageDirty();

	TSharedPtr<FJsonObject> R = MakeShared<FJsonObject>();
	R->SetBoolField(TEXT("success"), true);
	OutJson = SerializeJson(R);
}

void HandleRemoveConstraintFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString Path; Args->TryGetStringField(TEXT("physics_asset_path"), Path);
	UPhysicsAsset* Asset = LoadAsset(Path, OutError); if (!Asset) return;

	auto RemoveOne = [&](const FString& B1, const FString& B2, FString& OutErr) -> bool
	{
		UPhysicsConstraintTemplate* CT = FindConstraint(Asset, B1, B2);
		if (!CT) { OutErr = FString::Printf(TEXT("No constraint between '%s' and '%s'"), *B1, *B2); return false; }
		Asset->Modify();
		Asset->ConstraintSetup.Remove(CT);
		return true;
	};

	const TArray<TSharedPtr<FJsonValue>>* Items = nullptr;
	if (BatchToolHelper::TryGetBatchItems(Args, TEXT("items"), Items))
	{
		BatchToolHelper::FBatchResultBuilder Batch;
		for (int32 i = 0; i < Items->Num(); ++i)
		{
			const TSharedPtr<FJsonObject> Entry = (*Items)[i]->AsObject();
			if (!Entry.IsValid()) { Batch.AddFailure(i, TEXT("Invalid item")); continue; }
			FString B1, B2;
			Entry->TryGetStringField(TEXT("bone1"), B1);
			Entry->TryGetStringField(TEXT("bone2"), B2);
			FString Err;
			if (!RemoveOne(B1, B2, Err)) { Batch.AddFailure(i, Err); continue; }
			TSharedPtr<FJsonObject> R = MakeShared<FJsonObject>();
			R->SetStringField(TEXT("bone1"), B1);
			R->SetStringField(TEXT("bone2"), B2);
			Batch.AddSuccess(i, R);
		}
		Asset->MarkPackageDirty();
		Batch.Finalize(OutJson);
		return;
	}

	FString B1, B2;
	Args->TryGetStringField(TEXT("bone1"), B1);
	Args->TryGetStringField(TEXT("bone2"), B2);
	if (!RemoveOne(B1, B2, OutError)) return;
	Asset->MarkPackageDirty();

	TSharedPtr<FJsonObject> R = MakeShared<FJsonObject>();
	R->SetBoolField(TEXT("success"), true);
	OutJson = SerializeJson(R);
}

}
