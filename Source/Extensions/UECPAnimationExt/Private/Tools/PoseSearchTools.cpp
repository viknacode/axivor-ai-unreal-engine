// Copyright 2026, BlueprintsLab, All rights reserved.

#include "Tools/PoseSearchTools.h"
#include "Tools/BatchToolHelper.h"
#include "Managers/SettingsManager.h"

#include "PoseSearch/PoseSearchSchema.h"
#include "PoseSearch/PoseSearchDatabase.h"
#include "PoseSearch/PoseSearchFeatureChannel.h"
#include "PoseSearch/PoseSearchDerivedData.h"

#include "Animation/AnimSequence.h"
#include "Animation/BlendSpace.h"
#include "Animation/AnimComposite.h"
#include "Animation/AnimMontage.h"

#include "EditorAssetLibrary.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "UObject/Package.h"
#include "Misc/PackageName.h"
#include "UObject/UnrealType.h"
#include "Misc/EngineVersionComparison.h"
#if UE_VERSION_OLDER_THAN(5, 5, 0)
#include "InstancedStruct.h"
#else
#include "StructUtils/InstancedStruct.h"
#endif

namespace PoseSearchTools
{

static UObject* CreateDataAsset(const FString& AssetName, const FString& SavePath,
	UClass* AssetClass, FString& OutError)
{
	if (AssetName.IsEmpty()) { OutError = TEXT("name is required"); return nullptr; }
	FString PackagePath = SavePath;
	while (PackagePath.EndsWith(TEXT("/"))) PackagePath = PackagePath.LeftChop(1);
	PackagePath += TEXT("/") + AssetName;

	if (FPackageName::DoesPackageExist(PackagePath))
	{
		OutError = FString::Printf(TEXT("Asset already exists at '%s'"), *PackagePath);
		return nullptr;
	}

	UPackage* Pkg = CreatePackage(*PackagePath);
	UObject* NewAsset = NewObject<UObject>(Pkg, AssetClass, FName(*AssetName),
		RF_Public | RF_Standalone | RF_Transactional);
	if (!NewAsset) { OutError = TEXT("Failed to create asset object"); return nullptr; }

	FAssetRegistryModule::AssetCreated(NewAsset);
	NewAsset->MarkPackageDirty();
	return NewAsset;
}

static FArrayProperty* GetSkeletonsProp()
{
	static FArrayProperty* Cached = nullptr;
	if (!Cached)
		Cached = FindFProperty<FArrayProperty>(UPoseSearchSchema::StaticClass(), TEXT("Skeletons"));
	return Cached;
}

static FArrayProperty* GetChannelsProp()
{
	static FArrayProperty* Cached = nullptr;
	if (!Cached)
		Cached = FindFProperty<FArrayProperty>(UPoseSearchSchema::StaticClass(), TEXT("Channels"));
	return Cached;
}

static FArrayProperty* GetDatabaseAnimArrayProp(const UPoseSearchDatabase* Database)
{
	if (!Database) return nullptr;
	UClass* Cls = Database->GetClass();
	if (FArrayProperty* P = FindFProperty<FArrayProperty>(Cls, TEXT("DatabaseAnimationAssets"))) return P;
	if (FArrayProperty* P = FindFProperty<FArrayProperty>(Cls, TEXT("AnimationAssets"))) return P;
	return nullptr;
}

static bool ResolveDatabaseAnimEntry(FArrayProperty* ArrayProp, FScriptArrayHelper& Helper, int32 Index,
	UScriptStruct*& OutType, void*& OutData)
{
	OutType = nullptr; OutData = nullptr;
	if (!Helper.IsValidIndex(Index)) return false;
	void* Raw = Helper.GetRawPtr(Index);
	FStructProperty* InnerStruct = CastField<FStructProperty>(ArrayProp->Inner);
	if (!InnerStruct) return false;
	if (InnerStruct->Struct == FInstancedStruct::StaticStruct())
	{
		FInstancedStruct* IS = reinterpret_cast<FInstancedStruct*>(Raw);
		if (!IS || !IS->IsValid()) return false;
		OutType = const_cast<UScriptStruct*>(IS->GetScriptStruct());
		OutData = IS->GetMutableMemory();
	}
	else
	{
		OutType = InnerStruct->Struct;
		OutData = Raw;
	}
	return OutType != nullptr && OutData != nullptr;
}

static FString GetDatabaseAnimEntryAssetPath(FArrayProperty* ArrayProp, FScriptArrayHelper& Helper, int32 Index)
{
	UScriptStruct* Type = nullptr; void* Data = nullptr;
	if (!ResolveDatabaseAnimEntry(ArrayProp, Helper, Index, Type, Data)) return FString();
	for (TFieldIterator<FObjectPropertyBase> It(Type); It; ++It)
	{
		if (UObject* Obj = It->GetObjectPropertyValue_InContainer(Data))
			return Obj->GetPathName();
	}
	return FString();
}

void HandleCreatePoseSearchSchema(const FString& AssetName, const FString& SavePath,
	const FString& SkeletonPath, int32 SampleRate, FString& OutJson, FString& OutError)
{

	UClass* SchemaClass = UPoseSearchSchema::StaticClass();
	UPoseSearchSchema* Schema = Cast<UPoseSearchSchema>(
		CreateDataAsset(AssetName, SavePath, SchemaClass, OutError));
	if (!Schema) return;

	Schema->SampleRate = FMath::Clamp(SampleRate, 1, 240);

	if (!SkeletonPath.IsEmpty())
	{
		USkeleton* Skeleton = LoadObject<USkeleton>(nullptr, *SkeletonPath);
		if (Skeleton)
		{
			FArrayProperty* SkeletonsProp = GetSkeletonsProp();
			if (SkeletonsProp)
			{
				FScriptArrayHelper Helper(SkeletonsProp,
					SkeletonsProp->ContainerPtrToValuePtr<void>(Schema));
				const int32 NewIdx = Helper.AddValue();
				FPoseSearchRoledSkeleton* RoledSkel =
					reinterpret_cast<FPoseSearchRoledSkeleton*>(Helper.GetRawPtr(NewIdx));
				RoledSkel->Skeleton = Skeleton;
			}
		}
	}

	FString CleanPath = SavePath;
	while (CleanPath.EndsWith(TEXT("/"))) CleanPath = CleanPath.LeftChop(1);
	Schema->PostEditChange();
	Schema->MarkPackageDirty();
	const bool bSaved = UEditorAssetLibrary::SaveAsset(CleanPath + TEXT("/") + AssetName, false);
	OutJson = FString::Printf(TEXT("{\"success\":true,\"path\":\"%s/%s\",\"sample_rate\":%d,\"saved\":%s}"),
		*CleanPath, *AssetName, Schema->SampleRate, bSaved ? TEXT("true") : TEXT("false"));
}

void HandleAddPoseSearchChannel(const FString& SchemaPath, const FString& ChannelType,
	const TSharedPtr<FJsonObject>& Params, FString& OutJson, FString& OutError)
{

	UPoseSearchSchema* Schema = LoadObject<UPoseSearchSchema>(nullptr, *SchemaPath);
	if (!Schema)
	{
		OutError = FString::Printf(TEXT("Schema not found at '%s'"), *SchemaPath);
		return;
	}

	FString ClassName;
	if (ChannelType.Equals(TEXT("trajectory"), ESearchCase::IgnoreCase))
		ClassName = TEXT("/Script/PoseSearch.PoseSearchFeatureChannel_Trajectory");
	else if (ChannelType.Equals(TEXT("pose"), ESearchCase::IgnoreCase))
		ClassName = TEXT("/Script/PoseSearch.PoseSearchFeatureChannel_Pose");
	else if (ChannelType.Equals(TEXT("heading"), ESearchCase::IgnoreCase))
		ClassName = TEXT("/Script/PoseSearch.PoseSearchFeatureChannel_Heading");
	else
	{
		ClassName = ChannelType.Contains(TEXT("."))
			? ChannelType
			: FString::Printf(TEXT("/Script/PoseSearch.%s"), *ChannelType);
	}

	UClass* ChannelClass = FindObject<UClass>(nullptr, *ClassName);
	if (!ChannelClass)
	{
		OutError = FString::Printf(
			TEXT("Channel class not found: '%s'. Valid friendly names: trajectory, pose, heading"),
			*ChannelType);
		return;
	}

	if (!ChannelClass->IsChildOf(UPoseSearchFeatureChannel::StaticClass()))
	{
		OutError = FString::Printf(TEXT("Class '%s' is not a UPoseSearchFeatureChannel"), *ClassName);
		return;
	}

	UPoseSearchFeatureChannel* Channel = NewObject<UPoseSearchFeatureChannel>(
		Schema, ChannelClass, NAME_None, RF_Public | RF_Transactional);
	if (!Channel) { OutError = TEXT("Failed to create feature channel"); return; }

	FArrayProperty* ChannelsProp = GetChannelsProp();
	if (!ChannelsProp)
	{
		OutError = TEXT("Could not reflect Channels property on UPoseSearchSchema");
		return;
	}

	FScriptArrayHelper Helper(ChannelsProp,
		ChannelsProp->ContainerPtrToValuePtr<void>(Schema));
	const int32 NewIdx = Helper.AddValue();
	FObjectPropertyBase* Inner = CastField<FObjectPropertyBase>(ChannelsProp->Inner);
	if (Inner) Inner->SetObjectPropertyValue(Helper.GetRawPtr(NewIdx), Channel);

	Schema->PostEditChange();
	Schema->MarkPackageDirty();
	UEditorAssetLibrary::SaveAsset(SchemaPath, false);

	OutJson = FString::Printf(
		TEXT("{\"success\":true,\"channel_type\":\"%s\",\"channel_index\":%d,\"schema_path\":\"%s\"}"),
		*ChannelType, NewIdx, *SchemaPath);
}

void HandleCreatePoseSearchDatabase(const FString& AssetName, const FString& SavePath,
	const FString& SchemaPath, FString& OutJson, FString& OutError)
{

	UPoseSearchDatabase* Database = Cast<UPoseSearchDatabase>(
		CreateDataAsset(AssetName, SavePath, UPoseSearchDatabase::StaticClass(), OutError));
	if (!Database) return;

	if (!SchemaPath.IsEmpty())
	{
		UPoseSearchSchema* Schema = LoadObject<UPoseSearchSchema>(nullptr, *SchemaPath);
		if (Schema)
			Database->Schema = Schema;
	}

	FString CleanPath = SavePath;
	while (CleanPath.EndsWith(TEXT("/"))) CleanPath = CleanPath.LeftChop(1);
	Database->PostEditChange();
	Database->MarkPackageDirty();
	const bool bSaved = UEditorAssetLibrary::SaveAsset(CleanPath + TEXT("/") + AssetName, false);
	OutJson = FString::Printf(TEXT("{\"success\":true,\"path\":\"%s/%s\",\"schema_path\":\"%s\",\"saved\":%s}"),
		*CleanPath, *AssetName, *SchemaPath, bSaved ? TEXT("true") : TEXT("false"));
}

void HandleAddAnimationToDatabase(const FString& DatabasePath, const FString& AnimationType,
	const FString& AnimationPath, FString& OutJson, FString& OutError)
{

	UPoseSearchDatabase* Database = LoadObject<UPoseSearchDatabase>(nullptr, *DatabasePath);
	if (!Database)
	{
		OutError = FString::Printf(TEXT("PoseSearchDatabase not found at '%s'"), *DatabasePath);
		return;
	}

	const FString TypeLower = AnimationType.ToLower();

	UObject* AnimAsset = nullptr;
	if (TypeLower.IsEmpty() || TypeLower == TEXT("sequence"))
	{
		AnimAsset = LoadObject<UAnimSequence>(nullptr, *AnimationPath);
		if (!AnimAsset) { OutError = FString::Printf(TEXT("AnimSequence not found at '%s'"), *AnimationPath); return; }
	}
	else if (TypeLower == TEXT("blendspace"))
	{
		AnimAsset = LoadObject<UBlendSpace>(nullptr, *AnimationPath);
		if (!AnimAsset) { OutError = FString::Printf(TEXT("BlendSpace not found at '%s'"), *AnimationPath); return; }
	}
	else if (TypeLower == TEXT("composite"))
	{
		AnimAsset = LoadObject<UAnimComposite>(nullptr, *AnimationPath);
		if (!AnimAsset) { OutError = FString::Printf(TEXT("AnimComposite not found at '%s'"), *AnimationPath); return; }
	}
	else if (TypeLower == TEXT("montage"))
	{
		AnimAsset = LoadObject<UAnimMontage>(nullptr, *AnimationPath);
		if (!AnimAsset) { OutError = FString::Printf(TEXT("AnimMontage not found at '%s'"), *AnimationPath); return; }
	}
	else
	{
		OutError = FString::Printf(
			TEXT("Unknown animation type '%s'. Valid: sequence, blendspace, composite, montage"),
			*AnimationType);
		return;
	}

	int32 CountBefore = 0;
	if (FArrayProperty* P = GetDatabaseAnimArrayProp(Database))
	{ FScriptArrayHelper H(P, P->ContainerPtrToValuePtr<void>(Database)); CountBefore = H.Num(); }
	Database->Modify();

#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 7)
	FPoseSearchDatabaseAnimationAsset Entry;
	Entry.AnimAsset = AnimAsset;
	Database->AddAnimationAsset(Entry);
#else
	FInstancedStruct AnimStruct;
	if (TypeLower.IsEmpty() || TypeLower == TEXT("sequence"))
	{
		AnimStruct.InitializeAs<FPoseSearchDatabaseSequence>();
		AnimStruct.GetMutable<FPoseSearchDatabaseSequence>().Sequence = Cast<UAnimSequence>(AnimAsset);
	}
	else if (TypeLower == TEXT("blendspace"))
	{
		AnimStruct.InitializeAs<FPoseSearchDatabaseBlendSpace>();
		AnimStruct.GetMutable<FPoseSearchDatabaseBlendSpace>().BlendSpace = Cast<UBlendSpace>(AnimAsset);
	}
	else if (TypeLower == TEXT("composite"))
	{
		AnimStruct.InitializeAs<FPoseSearchDatabaseAnimComposite>();
		AnimStruct.GetMutable<FPoseSearchDatabaseAnimComposite>().AnimComposite = Cast<UAnimComposite>(AnimAsset);
	}
	else if (TypeLower == TEXT("montage"))
	{
		AnimStruct.InitializeAs<FPoseSearchDatabaseAnimMontage>();
		AnimStruct.GetMutable<FPoseSearchDatabaseAnimMontage>().AnimMontage = Cast<UAnimMontage>(AnimAsset);
	}
	Database->AddAnimationAsset(MoveTemp(AnimStruct));
#endif

	int32 CountAfter = CountBefore;
	if (FArrayProperty* P = GetDatabaseAnimArrayProp(Database))
	{ FScriptArrayHelper H(P, P->ContainerPtrToValuePtr<void>(Database)); CountAfter = H.Num(); }
	if (CountAfter <= CountBefore)
	{
		OutError = FString::Printf(
			TEXT("add_animation_to_database: '%s' was not added (entry count unchanged at %d). The asset may be an incompatible type for this database."),
			*AnimationPath, CountAfter);
		return;
	}

	Database->PostEditChange();
	Database->MarkPackageDirty();
	UEditorAssetLibrary::SaveAsset(DatabasePath, false);

	OutJson = FString::Printf(
		TEXT("{\"success\":true,\"type\":\"%s\",\"animation_path\":\"%s\",\"database_path\":\"%s\",\"animation_count\":%d}"),
		*AnimationType, *AnimationPath, *DatabasePath, CountAfter);
}

void HandleBuildPoseSearchDatabase(const FString& DatabasePath, FString& OutJson, FString& OutError)
{

	UPoseSearchDatabase* Database = LoadObject<UPoseSearchDatabase>(nullptr, *DatabasePath);
	if (!Database)
	{
		OutError = FString::Printf(TEXT("PoseSearchDatabase not found at '%s'"), *DatabasePath);
		return;
	}

	if (!Database->Schema)
	{
		OutError = FString::Printf(TEXT("Database '%s' has no Schema bound; set one with set_pose_search_database_schema before building."), *DatabasePath);
		return;
	}

	int32 AnimCount = 0;
	if (FArrayProperty* P = GetDatabaseAnimArrayProp(Database))
	{ FScriptArrayHelper H(P, P->ContainerPtrToValuePtr<void>(Database)); AnimCount = H.Num(); }

	Database->Modify();
	Database->PostEditChange();
	Database->MarkPackageDirty();

	// Synchronously (re)build the search index through the same derived-data path the editor uses.
	using namespace UE::PoseSearch;
	const EAsyncBuildIndexResult Result = FAsyncPoseSearchDatabasesManagement::RequestAsyncBuildIndex(
		Database, ERequestAsyncBuildFlag::NewRequest | ERequestAsyncBuildFlag::WaitForCompletion);

	const TCHAR* ResultStr = TEXT("Failed");
	if (Result == EAsyncBuildIndexResult::Success)         ResultStr = TEXT("Success");
	else if (Result == EAsyncBuildIndexResult::InProgress) ResultStr = TEXT("InProgress");

	const bool bSaved = UEditorAssetLibrary::SaveAsset(DatabasePath, false);

	if (Result == EAsyncBuildIndexResult::Failed)
	{
		OutError = FString::Printf(TEXT("Pose search index build FAILED for '%s' (%d animation entries). Check the schema channels/skeleton and the animation entries."), *DatabasePath, AnimCount);
		OutJson = FString::Printf(
			TEXT("{\"success\":false,\"error\":\"%s\",\"build_result\":\"%s\",\"animation_count\":%d,\"saved\":%s,\"path\":\"%s\"}"),
			*OutError.Replace(TEXT("\""), TEXT("\\\"")), ResultStr, AnimCount, bSaved ? TEXT("true") : TEXT("false"), *DatabasePath);
		return;
	}

	OutJson = FString::Printf(
		TEXT("{\"success\":true,\"build_result\":\"%s\",\"animation_count\":%d,\"saved\":%s,\"path\":\"%s\"}"),
		ResultStr, AnimCount, bSaved ? TEXT("true") : TEXT("false"), *DatabasePath);
}

void HandleGetPoseSearchSummary(const FString& AssetPath, FString& OutJson, FString& OutError)
{

	if (UPoseSearchSchema* Schema = LoadObject<UPoseSearchSchema>(nullptr, *AssetPath))
	{
		int32 SkeletonCount = 0;
		if (FArrayProperty* Prop = GetSkeletonsProp())
		{
			FScriptArrayHelper Helper(Prop, Prop->ContainerPtrToValuePtr<void>(Schema));
			SkeletonCount = Helper.Num();
		}

		int32 ChannelCount = 0;
		if (FArrayProperty* Prop = GetChannelsProp())
		{
			FScriptArrayHelper Helper(Prop, Prop->ContainerPtrToValuePtr<void>(Schema));
			ChannelCount = Helper.Num();
		}

		OutJson = FString::Printf(
			TEXT("{\"success\":true,\"asset_type\":\"schema\",\"sample_rate\":%d,\"skeleton_count\":%d,\"channel_count\":%d}"),
			Schema->SampleRate, SkeletonCount, ChannelCount);
		return;
	}

	if (UPoseSearchDatabase* Database = LoadObject<UPoseSearchDatabase>(nullptr, *AssetPath))
	{
		const FString SchemaPath = Database->Schema
			? Database->Schema->GetPathName()
			: TEXT("");

		int32 AnimCount = 0;
		FString AnimsJson = TEXT("[");
		if (FArrayProperty* Prop = GetDatabaseAnimArrayProp(Database))
		{
			FScriptArrayHelper Helper(Prop, Prop->ContainerPtrToValuePtr<void>(Database));
			AnimCount = Helper.Num();
			for (int32 i = 0; i < AnimCount; ++i)
			{
				FString Path = GetDatabaseAnimEntryAssetPath(Prop, Helper, i);
				if (i > 0) AnimsJson += TEXT(",");
				AnimsJson += TEXT("\"") + Path.Replace(TEXT("\""), TEXT("\\\"")) + TEXT("\"");
			}
		}
		AnimsJson += TEXT("]");

		OutJson = FString::Printf(
			TEXT("{\"success\":true,\"asset_type\":\"database\",\"schema_path\":\"%s\",\"animation_count\":%d,\"animations\":%s}"),
			*SchemaPath, AnimCount, *AnimsJson);
		return;
	}

	OutError = FString::Printf(
		TEXT("Asset not found or not a PoseSearch asset at '%s'"), *AssetPath);
}

void HandleSetPoseSearchChannelProperty(const FString& SchemaPath, int32 ChannelIndex,
	const FString& PropertyName, const FString& PropertyValue, FString& OutJson, FString& OutError)
{

	UPoseSearchSchema* Schema = LoadObject<UPoseSearchSchema>(nullptr, *SchemaPath);
	if (!Schema) { OutError = FString::Printf(TEXT("Schema not found at '%s'"), *SchemaPath); return; }

	FArrayProperty* ChannelsProp = GetChannelsProp();
	if (!ChannelsProp) { OutError = TEXT("Could not reflect Channels on UPoseSearchSchema"); return; }

	FScriptArrayHelper Helper(ChannelsProp, ChannelsProp->ContainerPtrToValuePtr<void>(Schema));
	if (!Helper.IsValidIndex(ChannelIndex))
	{
		OutError = FString::Printf(TEXT("Channel index %d out of range (count: %d)"), ChannelIndex, Helper.Num());
		return;
	}

	FObjectPropertyBase* Inner = CastField<FObjectPropertyBase>(ChannelsProp->Inner);
	if (!Inner) { OutError = TEXT("Channels inner is not an object property"); return; }

	UObject* ChannelObj = Inner->GetObjectPropertyValue(Helper.GetRawPtr(ChannelIndex));
	if (!ChannelObj)
	{
		OutError = FString::Printf(TEXT("Channel at index %d is null"), ChannelIndex);
		return;
	}

	FProperty* Prop = FindFProperty<FProperty>(ChannelObj->GetClass(), *PropertyName);
	if (!Prop)
	{
		TArray<FString> Names;
		for (TFieldIterator<FProperty> It(ChannelObj->GetClass()); It; ++It)
			Names.Add(It->GetName());
		OutError = FString::Printf(TEXT("Property '%s' not found on '%s'. Available: %s"),
			*PropertyName, *ChannelObj->GetClass()->GetName(), *FString::Join(Names, TEXT(", ")));
		return;
	}

	ChannelObj->Modify();
	void* PropPtr = Prop->ContainerPtrToValuePtr<void>(ChannelObj);
	const TCHAR* ImportResult = Prop->ImportText_Direct(*PropertyValue, PropPtr, ChannelObj, PPF_None, nullptr);
	if (!ImportResult)
	{
		OutError = FString::Printf(TEXT("Failed to set '%s': import failed for value '%s'"), *PropertyName, *PropertyValue);
		return;
	}

	ChannelObj->PostEditChange();
	Schema->Modify();
	Schema->PostEditChange();
	Schema->MarkPackageDirty();
	UEditorAssetLibrary::SaveAsset(SchemaPath, false);
	OutJson = FString::Printf(
		TEXT("{\"success\":true,\"channel_index\":%d,\"channel_class\":\"%s\",\"property\":\"%s\",\"value\":\"%s\"}"),
		ChannelIndex, *ChannelObj->GetClass()->GetName(), *PropertyName, *PropertyValue);
}

void HandleSetPoseSearchSchemaSkeleton(const FString& SchemaPath, const FString& SkeletonPath,
	int32 SkeletonIndex, const FString& Role, FString& OutJson, FString& OutError)
{

	UPoseSearchSchema* Schema = LoadObject<UPoseSearchSchema>(nullptr, *SchemaPath);
	if (!Schema) { OutError = FString::Printf(TEXT("Schema not found at '%s'"), *SchemaPath); return; }

	USkeleton* Skeleton = LoadObject<USkeleton>(nullptr, *SkeletonPath);
	if (!Skeleton) { OutError = FString::Printf(TEXT("Skeleton not found at '%s'"), *SkeletonPath); return; }

	FArrayProperty* SkeletonsProp = GetSkeletonsProp();
	if (!SkeletonsProp) { OutError = TEXT("Could not reflect Skeletons on UPoseSearchSchema"); return; }

	FScriptArrayHelper Helper(SkeletonsProp, SkeletonsProp->ContainerPtrToValuePtr<void>(Schema));

	int32 TargetIdx = SkeletonIndex;
	if (TargetIdx < 0 || TargetIdx >= Helper.Num())
		TargetIdx = Helper.AddValue();

	FPoseSearchRoledSkeleton* RoledSkel =
		reinterpret_cast<FPoseSearchRoledSkeleton*>(Helper.GetRawPtr(TargetIdx));
	RoledSkel->Skeleton = Skeleton;

	if (!Role.IsEmpty())
	{
		if (FEnumProperty* RoleProp = FindFProperty<FEnumProperty>(
			FPoseSearchRoledSkeleton::StaticStruct(), TEXT("Role")))
		{
			RoleProp->ImportText_Direct(*Role,
				RoleProp->ContainerPtrToValuePtr<void>(RoledSkel), nullptr, PPF_None);
		}
	}

	Schema->PostEditChange();
	Schema->MarkPackageDirty();
	UEditorAssetLibrary::SaveAsset(SchemaPath, false);
	OutJson = FString::Printf(
		TEXT("{\"success\":true,\"skeleton_index\":%d,\"skeleton_path\":\"%s\",\"role\":\"%s\"}"),
		TargetIdx, *SkeletonPath, *Role);
}

void HandleRemoveAnimationFromDatabase(const FString& DatabasePath, int32 AnimationIndex,
	FString& OutJson, FString& OutError)
{

	UPoseSearchDatabase* Database = LoadObject<UPoseSearchDatabase>(nullptr, *DatabasePath);
	if (!Database)
	{
		OutError = FString::Printf(TEXT("PoseSearchDatabase not found at '%s'"), *DatabasePath);
		return;
	}

	FArrayProperty* AnimProp = GetDatabaseAnimArrayProp(Database);
	if (!AnimProp) { OutError = TEXT("Could not reflect the database animation array (DatabaseAnimationAssets/AnimationAssets)"); return; }

	FScriptArrayHelper Helper(AnimProp, AnimProp->ContainerPtrToValuePtr<void>(Database));
	if (!Helper.IsValidIndex(AnimationIndex))
	{
		OutError = FString::Printf(TEXT("Animation index %d out of range (count: %d)"),
			AnimationIndex, Helper.Num());
		return;
	}

	Database->Modify();
	Helper.RemoveValues(AnimationIndex, 1);
	Database->PostEditChange();
	Database->MarkPackageDirty();
	UEditorAssetLibrary::SaveAsset(DatabasePath, false);

	OutJson = FString::Printf(
		TEXT("{\"success\":true,\"removed_index\":%d,\"remaining_count\":%d,\"database_path\":\"%s\"}"),
		AnimationIndex, Helper.Num(), *DatabasePath);
}

void HandleSetPoseSearchSchemaProperty(const FString& SchemaPath, const FString& PropertyName,
	const FString& PropertyValue, FString& OutJson, FString& OutError)
{

	UPoseSearchSchema* Schema = LoadObject<UPoseSearchSchema>(nullptr, *SchemaPath);
	if (!Schema) { OutError = FString::Printf(TEXT("Schema not found at '%s'"), *SchemaPath); return; }

	if (PropertyName.Equals(TEXT("SampleRate"), ESearchCase::IgnoreCase))
	{
		Schema->SampleRate = FMath::Clamp(FCString::Atoi(*PropertyValue), 1, 240);
		Schema->MarkPackageDirty();
		OutJson = FString::Printf(TEXT("{\"success\":true,\"property\":\"SampleRate\",\"value\":%d}"),
			Schema->SampleRate);
		return;
	}

	FProperty* Prop = FindFProperty<FProperty>(UPoseSearchSchema::StaticClass(), *PropertyName);
	if (!Prop)
	{
		TArray<FString> Names;
		for (TFieldIterator<FProperty> It(UPoseSearchSchema::StaticClass()); It; ++It)
			if (!It->HasAnyPropertyFlags(CPF_Deprecated)) Names.Add(It->GetName());
		OutError = FString::Printf(TEXT("Property '%s' not found on UPoseSearchSchema. Available: %s"),
			*PropertyName, *FString::Join(Names, TEXT(", ")));
		return;
	}

	void* PropPtr = Prop->ContainerPtrToValuePtr<void>(Schema);
	if (FObjectPropertyBase* ObjProp = CastField<FObjectPropertyBase>(Prop))
	{
		UObject* Asset = LoadObject<UObject>(nullptr, *PropertyValue);
		if (!Asset && !PropertyValue.IsEmpty())
			{ OutError = FString::Printf(TEXT("Asset not found at '%s'"), *PropertyValue); return; }
		ObjProp->SetObjectPropertyValue(PropPtr, Asset);
	}
	else
	{
		const TCHAR* R = Prop->ImportText_Direct(*PropertyValue, PropPtr, Schema, PPF_None, nullptr);
		if (!R)
			{ OutError = FString::Printf(TEXT("Failed to set '%s': import failed for value '%s'"), *PropertyName, *PropertyValue); return; }
	}

	Schema->Modify();
	Schema->PostEditChange();
	Schema->MarkPackageDirty();
	UEditorAssetLibrary::SaveAsset(SchemaPath, false);
	OutJson = FString::Printf(TEXT("{\"success\":true,\"property\":\"%s\",\"value\":\"%s\"}"),
		*PropertyName, *PropertyValue);
}

void HandleRemovePoseSearchChannel(const FString& SchemaPath, int32 ChannelIndex,
	FString& OutJson, FString& OutError)
{

	UPoseSearchSchema* Schema = LoadObject<UPoseSearchSchema>(nullptr, *SchemaPath);
	if (!Schema) { OutError = FString::Printf(TEXT("Schema not found at '%s'"), *SchemaPath); return; }

	FArrayProperty* ChannelsProp = GetChannelsProp();
	if (!ChannelsProp) { OutError = TEXT("Could not reflect Channels on UPoseSearchSchema"); return; }

	FScriptArrayHelper Helper(ChannelsProp, ChannelsProp->ContainerPtrToValuePtr<void>(Schema));
	if (!Helper.IsValidIndex(ChannelIndex))
	{
		OutError = FString::Printf(TEXT("Channel index %d out of range (count: %d)"), ChannelIndex, Helper.Num());
		return;
	}

	Helper.RemoveValues(ChannelIndex, 1);
	Schema->PostEditChange();
	Schema->MarkPackageDirty();
	UEditorAssetLibrary::SaveAsset(SchemaPath, false);
	OutJson = FString::Printf(
		TEXT("{\"success\":true,\"removed_channel_index\":%d,\"remaining_channels\":%d}"),
		ChannelIndex, Helper.Num());
}

void HandleSetAnimationDatabaseEntryProperty(const FString& DatabasePath, int32 AnimationIndex,
	const FString& PropertyName, const FString& PropertyValue, FString& OutJson, FString& OutError)
{

	UPoseSearchDatabase* Database = LoadObject<UPoseSearchDatabase>(nullptr, *DatabasePath);
	if (!Database) { OutError = FString::Printf(TEXT("Database not found at '%s'"), *DatabasePath); return; }

	FArrayProperty* AnimProp = GetDatabaseAnimArrayProp(Database);
	if (!AnimProp) { OutError = TEXT("Could not reflect the database animation array (DatabaseAnimationAssets/AnimationAssets)"); return; }

	FScriptArrayHelper Helper(AnimProp, AnimProp->ContainerPtrToValuePtr<void>(Database));
	if (!Helper.IsValidIndex(AnimationIndex))
	{
		OutError = FString::Printf(TEXT("Animation index %d out of range (count: %d)"),
			AnimationIndex, Helper.Num());
		return;
	}

	UScriptStruct* EntryType = nullptr; void* EntryData = nullptr;
	if (!ResolveDatabaseAnimEntry(AnimProp, Helper, AnimationIndex, EntryType, EntryData))
		{ OutError = FString::Printf(TEXT("Animation entry at index %d is invalid"), AnimationIndex); return; }

	FProperty* Prop = FindFProperty<FProperty>(EntryType, *PropertyName);
	if (!Prop)
	{
		TArray<FString> Names;
		for (TFieldIterator<FProperty> It(EntryType); It; ++It) Names.Add(It->GetName());
		OutError = FString::Printf(TEXT("Property '%s' not found on '%s'. Available: %s"),
			*PropertyName, *EntryType->GetName(), *FString::Join(Names, TEXT(", ")));
		return;
	}

	void* PropPtr = Prop->ContainerPtrToValuePtr<void>(EntryData);
	if (FObjectPropertyBase* ObjProp = CastField<FObjectPropertyBase>(Prop))
	{
		UObject* Asset = LoadObject<UObject>(nullptr, *PropertyValue);
		if (!Asset && !PropertyValue.IsEmpty())
			{ OutError = FString::Printf(TEXT("Asset not found at '%s'"), *PropertyValue); return; }
		ObjProp->SetObjectPropertyValue(PropPtr, Asset);
	}
	else if (FBoolProperty* BoolProp = CastField<FBoolProperty>(Prop))
	{
		BoolProp->SetPropertyValue(PropPtr,
			PropertyValue.Equals(TEXT("true"), ESearchCase::IgnoreCase) || PropertyValue == TEXT("1"));
	}
	else
	{
		const TCHAR* R = Prop->ImportText_Direct(*PropertyValue, PropPtr, nullptr, PPF_None, nullptr);
		if (!R)
			{ OutError = FString::Printf(TEXT("Failed to set '%s': import failed for '%s'"), *PropertyName, *PropertyValue); return; }
	}

	Database->Modify();
	Database->PostEditChange();
	Database->MarkPackageDirty();
	UEditorAssetLibrary::SaveAsset(DatabasePath, false);
	OutJson = FString::Printf(
		TEXT("{\"success\":true,\"animation_index\":%d,\"entry_type\":\"%s\",\"property\":\"%s\",\"value\":\"%s\"}"),
		AnimationIndex, *EntryType->GetName(), *PropertyName, *PropertyValue);
}

void HandleSetPoseSearchDatabaseSchema(const FString& DatabasePath, const FString& SchemaPath,
	FString& OutJson, FString& OutError)
{

	UPoseSearchDatabase* Database = LoadObject<UPoseSearchDatabase>(nullptr, *DatabasePath);
	if (!Database) { OutError = FString::Printf(TEXT("Database not found at '%s'"), *DatabasePath); return; }

	UPoseSearchSchema* Schema = LoadObject<UPoseSearchSchema>(nullptr, *SchemaPath);
	if (!Schema) { OutError = FString::Printf(TEXT("Schema not found at '%s'"), *SchemaPath); return; }

	FProperty* SchemaProp = FindFProperty<FProperty>(UPoseSearchDatabase::StaticClass(), TEXT("Schema"));
	if (!SchemaProp)
		{ OutError = TEXT("Schema property not found on UPoseSearchDatabase via reflection"); return; }

	FObjectPropertyBase* ObjProp = CastField<FObjectPropertyBase>(SchemaProp);
	if (!ObjProp)
		{ OutError = TEXT("Schema property is not an object property"); return; }

	ObjProp->SetObjectPropertyValue(ObjProp->ContainerPtrToValuePtr<void>(Database), Schema);
	Database->Modify();
	Database->PostEditChange();
	Database->MarkPackageDirty();
	UEditorAssetLibrary::SaveAsset(DatabasePath, false);
	OutJson = FString::Printf(
		TEXT("{\"success\":true,\"database_path\":\"%s\",\"new_schema_path\":\"%s\"}"),
		*DatabasePath, *SchemaPath);
}

void HandleGetPoseSearchChannelProperties(const FString& SchemaPath, int32 ChannelIndex,
	FString& OutJson, FString& OutError)
{

	UPoseSearchSchema* Schema = LoadObject<UPoseSearchSchema>(nullptr, *SchemaPath);
	if (!Schema) { OutError = FString::Printf(TEXT("Schema not found at '%s'"), *SchemaPath); return; }

	FArrayProperty* ChannelsProp = GetChannelsProp();
	if (!ChannelsProp) { OutError = TEXT("Could not reflect Channels on UPoseSearchSchema"); return; }

	FScriptArrayHelper Helper(ChannelsProp, ChannelsProp->ContainerPtrToValuePtr<void>(Schema));
	if (!Helper.IsValidIndex(ChannelIndex))
	{
		OutError = FString::Printf(TEXT("Channel index %d out of range (count: %d)"),
			ChannelIndex, Helper.Num());
		return;
	}

	FObjectPropertyBase* Inner = CastField<FObjectPropertyBase>(ChannelsProp->Inner);
	if (!Inner) { OutError = TEXT("Channels inner is not an object property"); return; }

	UObject* ChannelObj = Inner->GetObjectPropertyValue(Helper.GetRawPtr(ChannelIndex));
	if (!ChannelObj)
		{ OutError = FString::Printf(TEXT("Channel at index %d is null"), ChannelIndex); return; }

	FString PropsJson = TEXT("[");
	bool bFirst = true;
	for (TFieldIterator<FProperty> It(ChannelObj->GetClass()); It; ++It)
	{
		FProperty* Prop = *It;
		FString Value;
		Prop->ExportTextItem_Direct(Value, Prop->ContainerPtrToValuePtr<void>(ChannelObj),
			nullptr, nullptr, PPF_None);
		FString SafeValue = Value.Replace(TEXT("\""), TEXT("\\\""));
		if (!bFirst) PropsJson += TEXT(",");
		PropsJson += FString::Printf(TEXT("{\"name\":\"%s\",\"type\":\"%s\",\"value\":\"%s\"}"),
			*Prop->GetName(), *Prop->GetClass()->GetName(), *SafeValue);
		bFirst = false;
	}
	PropsJson += TEXT("]");

	OutJson = FString::Printf(
		TEXT("{\"success\":true,\"channel_index\":%d,\"channel_class\":\"%s\",\"properties\":%s}"),
		ChannelIndex, *ChannelObj->GetClass()->GetName(), *PropsJson);
}

void HandleCreatePoseSearchNormalizationSet(const FString& AssetName, const FString& SavePath,
	FString& OutJson, FString& OutError)
{

	UClass* NormClass = FindObject<UClass>(nullptr, TEXT("/Script/PoseSearch.PoseSearchNormalizationSet"));
	if (!NormClass)
	{
		OutError = TEXT("PoseSearchNormalizationSet class not found — ensure the PoseSearch plugin is enabled");
		return;
	}

	UObject* Asset = CreateDataAsset(AssetName, SavePath, NormClass, OutError);
	if (!Asset) return;

	FString CleanPath = SavePath;
	while (CleanPath.EndsWith(TEXT("/"))) CleanPath = CleanPath.LeftChop(1);
	const FString FullPath = CleanPath + TEXT("/") + AssetName;
	Asset->PostEditChange();
	Asset->MarkPackageDirty();
	const bool bSaved = UEditorAssetLibrary::SaveAsset(FullPath, false);
	OutJson = FString::Printf(
		TEXT("{\"success\":true,\"asset_path\":\"%s\",\"class\":\"PoseSearchNormalizationSet\",\"saved\":%s}"),
		*FullPath, bSaved ? TEXT("true") : TEXT("false"));
}

void HandleAddDatabaseToNormalizationSet(const FString& NormSetPath, const FString& DatabasePath,
	FString& OutJson, FString& OutError)
{

	UObject* NormSet = LoadObject<UObject>(nullptr, *NormSetPath);
	if (!NormSet) { OutError = FString::Printf(TEXT("NormalizationSet not found at '%s'"), *NormSetPath); return; }

	UObject* DB = LoadObject<UObject>(nullptr, *DatabasePath);
	if (!DB) { OutError = FString::Printf(TEXT("Database not found at '%s'"), *DatabasePath); return; }

	static const TArray<FName> DbPropNames = { TEXT("Databases"), TEXT("PoseSearchDatabases"), TEXT("Assets") };
	FArrayProperty* DbsProp = nullptr;
	for (const FName& N : DbPropNames)
	{
		DbsProp = FindFProperty<FArrayProperty>(NormSet->GetClass(), N);
		if (DbsProp) break;
	}
	if (!DbsProp)
	{
		OutError = TEXT("Could not find Databases array on PoseSearchNormalizationSet — property name may differ in this UE version");
		return;
	}

	FObjectPropertyBase* Inner = CastField<FObjectPropertyBase>(DbsProp->Inner);
	if (!Inner) { OutError = TEXT("Databases inner property is not an object reference"); return; }

	FScriptArrayHelper Helper(DbsProp, DbsProp->ContainerPtrToValuePtr<void>(NormSet));
	for (int32 i = 0; i < Helper.Num(); ++i)
	{
		if (Inner->GetObjectPropertyValue(Helper.GetRawPtr(i)) == DB)
		{
			OutJson = FString::Printf(
				TEXT("{\"success\":true,\"message\":\"Already present\",\"database_count\":%d}"), Helper.Num());
			return;
		}
	}

	Helper.AddValue();
	Inner->SetObjectPropertyValue(Helper.GetRawPtr(Helper.Num() - 1), DB);
	NormSet->Modify();
	NormSet->PostEditChange();
	NormSet->MarkPackageDirty();
	UEditorAssetLibrary::SaveAsset(NormSetPath, false);

	OutJson = FString::Printf(
		TEXT("{\"success\":true,\"database_count\":%d}"), Helper.Num());
}

void HandleSetSchemaMirrorDataTable(const FString& SchemaPath, const FString& MirrorTablePath,
	FString& OutJson, FString& OutError)
{

	UPoseSearchSchema* Schema = LoadObject<UPoseSearchSchema>(nullptr, *SchemaPath);
	if (!Schema) { OutError = FString::Printf(TEXT("Schema not found at '%s'"), *SchemaPath); return; }

	static const TArray<FName> MirrorPropNames = { TEXT("MirrorDataTable"), TEXT("MirrorTable") };
	FObjectPropertyBase* MirrorProp = nullptr;
	for (const FName& N : MirrorPropNames)
	{
		MirrorProp = FindFProperty<FObjectPropertyBase>(Schema->GetClass(), N);
		if (MirrorProp) break;
	}
	if (!MirrorProp)
	{
		OutError = TEXT("MirrorDataTable property not found on UPoseSearchSchema");
		return;
	}

	if (MirrorTablePath.IsEmpty())
	{
		MirrorProp->SetObjectPropertyValue(MirrorProp->ContainerPtrToValuePtr<void>(Schema), nullptr);
	}
	else
	{
		UObject* MirrorTable = LoadObject<UObject>(nullptr, *MirrorTablePath);
		if (!MirrorTable) { OutError = FString::Printf(TEXT("MirrorDataTable not found at '%s'"), *MirrorTablePath); return; }
		MirrorProp->SetObjectPropertyValue(MirrorProp->ContainerPtrToValuePtr<void>(Schema), MirrorTable);
	}

	Schema->Modify();
	Schema->PostEditChange();
	Schema->MarkPackageDirty();
	UEditorAssetLibrary::SaveAsset(SchemaPath, false);
	OutJson = FString::Printf(
		TEXT("{\"success\":true,\"mirror_table\":\"%s\"}"), *MirrorTablePath);
}

void HandleDuplicatePoseSearchChannel(const FString& SchemaPath, int32 ChannelIndex,
	FString& OutJson, FString& OutError)
{

	UPoseSearchSchema* Schema = LoadObject<UPoseSearchSchema>(nullptr, *SchemaPath);
	if (!Schema) { OutError = FString::Printf(TEXT("Schema not found at '%s'"), *SchemaPath); return; }

	FArrayProperty* ChannelsProp = GetChannelsProp();
	if (!ChannelsProp) { OutError = TEXT("Could not reflect Channels on UPoseSearchSchema"); return; }

	FScriptArrayHelper Helper(ChannelsProp, ChannelsProp->ContainerPtrToValuePtr<void>(Schema));
	if (!Helper.IsValidIndex(ChannelIndex))
	{
		OutError = FString::Printf(TEXT("Channel index %d out of range (count: %d)"), ChannelIndex, Helper.Num());
		return;
	}

	FObjectPropertyBase* Inner = CastField<FObjectPropertyBase>(ChannelsProp->Inner);
	if (!Inner) { OutError = TEXT("Channels inner is not an object property"); return; }

	UObject* SrcChannel = Inner->GetObjectPropertyValue(Helper.GetRawPtr(ChannelIndex));
	if (!SrcChannel) { OutError = FString::Printf(TEXT("Channel at index %d is null"), ChannelIndex); return; }

	UObject* NewChannel = DuplicateObject<UObject>(SrcChannel, Schema);
	if (!NewChannel) { OutError = TEXT("DuplicateObject failed for channel"); return; }

	Helper.AddValue();
	Inner->SetObjectPropertyValue(Helper.GetRawPtr(Helper.Num() - 1), NewChannel);

	Schema->Modify();
	Schema->PostEditChange();
	Schema->MarkPackageDirty();
	UEditorAssetLibrary::SaveAsset(SchemaPath, false);
	OutJson = FString::Printf(
		TEXT("{\"success\":true,\"source_index\":%d,\"new_index\":%d,\"channel_count\":%d,\"channel_class\":\"%s\"}"),
		ChannelIndex, Helper.Num() - 1, Helper.Num(), *SrcChannel->GetClass()->GetName());
}

void HandleReorderPoseSearchChannels(const FString& SchemaPath, int32 FromIndex, int32 ToIndex,
	FString& OutJson, FString& OutError)
{

	if (FromIndex == ToIndex)
	{
		OutJson = TEXT("{\"success\":true,\"message\":\"No change needed\"}");
		return;
	}

	UPoseSearchSchema* Schema = LoadObject<UPoseSearchSchema>(nullptr, *SchemaPath);
	if (!Schema) { OutError = FString::Printf(TEXT("Schema not found at '%s'"), *SchemaPath); return; }

	FArrayProperty* ChannelsProp = GetChannelsProp();
	if (!ChannelsProp) { OutError = TEXT("Could not reflect Channels on UPoseSearchSchema"); return; }

	FScriptArrayHelper Helper(ChannelsProp, ChannelsProp->ContainerPtrToValuePtr<void>(Schema));
	if (!Helper.IsValidIndex(FromIndex) || !Helper.IsValidIndex(ToIndex))
	{
		OutError = FString::Printf(TEXT("Index out of range: from=%d to=%d (count=%d)"),
			FromIndex, ToIndex, Helper.Num());
		return;
	}

	const int32 ElemSz = static_cast<int32>(ChannelsProp->Inner->GetSize());
	if (FromIndex < ToIndex)
		for (int32 i = FromIndex; i < ToIndex; ++i) FMemory::Memswap(Helper.GetRawPtr(i), Helper.GetRawPtr(i + 1), ElemSz);
	else
		for (int32 i = FromIndex; i > ToIndex; --i) FMemory::Memswap(Helper.GetRawPtr(i), Helper.GetRawPtr(i - 1), ElemSz);

	Schema->Modify();
	Schema->PostEditChange();
	Schema->MarkPackageDirty();
	UEditorAssetLibrary::SaveAsset(SchemaPath, false);
	OutJson = FString::Printf(TEXT("{\"success\":true,\"moved_from\":%d,\"moved_to\":%d}"), FromIndex, ToIndex);
}

void HandleListPoseSearchChannelTypes(FString& OutJson, FString& OutError)
{

	UClass* BaseClass = UPoseSearchFeatureChannel::StaticClass();

	FString TypesJson = TEXT("[");
	bool bFirst = true;
	for (TObjectIterator<UClass> It; It; ++It)
	{
		UClass* C = *It;
		if (!C || !C->IsChildOf(BaseClass) || C == BaseClass) continue;
		if (C->HasAnyClassFlags(CLASS_Abstract)) continue;

		if (!bFirst) TypesJson += TEXT(",");
		FString SafeName = C->GetName().Replace(TEXT("\""), TEXT("\\\""));
		FString SafePath = C->GetPathName().Replace(TEXT("\""), TEXT("\\\""));
		TypesJson += FString::Printf(TEXT("{\"name\":\"%s\",\"path\":\"%s\"}"), *SafeName, *SafePath);
		bFirst = false;
	}
	TypesJson += TEXT("]");

	OutJson = FString::Printf(TEXT("{\"success\":true,\"channel_types\":%s}"), *TypesJson);
}

void HandleAddAnimationToDatabaseFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError)
{
	FString DatabasePath; Args->TryGetStringField(TEXT("database_path"), DatabasePath);

	const TArray<TSharedPtr<FJsonValue>>* ItemsArray = nullptr;
	if (BatchToolHelper::TryGetBatchItems(Args, TEXT("animations"), ItemsArray))
	{
		BatchToolHelper::FBatchResultBuilder Batch;
		for (int32 i = 0; i < ItemsArray->Num(); i++)
		{
			FString AnimPath, AnimType = TEXT("sequence");
			TSharedPtr<FJsonObject> Item = (*ItemsArray)[i]->AsObject();
			if (Item.IsValid())
			{
				AnimPath = BatchToolHelper::GetItemString(Item, TEXT("animation_path"), TEXT("path"));
				FString AT = BatchToolHelper::GetItemString(Item, TEXT("animation_type"));
				if (!AT.IsEmpty()) AnimType = AT;
			}
			else
			{
				(*ItemsArray)[i]->TryGetString(AnimPath);
			}
			if (AnimPath.IsEmpty()) { Batch.AddFailure(i, TEXT("Missing animation_path")); continue; }
			FString ItemOut, ItemErr;
			HandleAddAnimationToDatabase(DatabasePath, AnimType, AnimPath, ItemOut, ItemErr);
			if (ItemErr.IsEmpty())
			{
				auto Extra = MakeShared<FJsonObject>(); Extra->SetStringField(TEXT("animation_path"), AnimPath);
				Batch.AddSuccess(i, Extra);
			}
			else Batch.AddFailure(i, ItemErr);
		}
		Batch.Finalize(OutJson);
		return;
	}
	FString AnimType = TEXT("sequence"), AnimPath;
	Args->TryGetStringField(TEXT("animation_type"), AnimType); Args->TryGetStringField(TEXT("animation_path"), AnimPath);
	HandleAddAnimationToDatabase(DatabasePath, AnimType, AnimPath, OutJson, OutError);
}

void HandleAddPoseSearchChannelFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError)
{
	FString SchemaPath; Args->TryGetStringField(TEXT("schema_path"), SchemaPath);

	const TArray<TSharedPtr<FJsonValue>>* ItemsArray = nullptr;
	if (BatchToolHelper::TryGetBatchItems(Args, TEXT("channels"), ItemsArray))
	{
		BatchToolHelper::FBatchResultBuilder Batch;
		for (int32 i = 0; i < ItemsArray->Num(); i++)
		{
			FString ChannelType;
			TSharedPtr<FJsonObject> Item = (*ItemsArray)[i]->AsObject();
			if (Item.IsValid())
				ChannelType = BatchToolHelper::GetItemString(Item, TEXT("channel_type"), TEXT("type"));
			else
				(*ItemsArray)[i]->TryGetString(ChannelType);
			if (ChannelType.IsEmpty()) { Batch.AddFailure(i, TEXT("Missing channel_type")); continue; }
			TSharedPtr<FJsonObject> ItemArgs = MakeShared<FJsonObject>();
			ItemArgs->SetStringField(TEXT("schema_path"), SchemaPath);
			ItemArgs->SetStringField(TEXT("channel_type"), ChannelType);
			if (Item.IsValid())
			{
				for (const auto& KV : Item->Values)
				{
					const FString K(*KV.Key);
					if (K != TEXT("channel_type") && K != TEXT("type"))
						ItemArgs->SetField(K, KV.Value);
				}
			}
			FString ItemOut, ItemErr;
			HandleAddPoseSearchChannel(SchemaPath, ChannelType, ItemArgs, ItemOut, ItemErr);
			if (ItemErr.IsEmpty())
			{
				auto Extra = MakeShared<FJsonObject>(); Extra->SetStringField(TEXT("channel_type"), ChannelType);
				Batch.AddSuccess(i, Extra);
			}
			else Batch.AddFailure(i, ItemErr);
		}
		Batch.Finalize(OutJson);
		return;
	}
	FString ChannelType; Args->TryGetStringField(TEXT("channel_type"), ChannelType);
	HandleAddPoseSearchChannel(SchemaPath, ChannelType, Args, OutJson, OutError);
}

void HandleCreatePoseSearchSchemaFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJson, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString AssetName = TEXT("PSSchema"), SavePath, SkeletonPath;
	int32 SampleRate = 30;
	Args->TryGetStringField(TEXT("asset_name"), AssetName);
	Args->TryGetStringField(TEXT("save_path"), SavePath);
	if (SavePath.IsEmpty()) SavePath = FSettingsManager::GetDefaultSavePath();
	Args->TryGetStringField(TEXT("skeleton_path"), SkeletonPath);
	Args->TryGetNumberField(TEXT("sample_rate"), SampleRate);
	HandleCreatePoseSearchSchema(AssetName, SavePath, SkeletonPath, SampleRate, OutJson, OutError);
}

void HandleCreatePoseSearchDatabaseFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJson, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString AssetName = TEXT("PSDatabase"), SavePath, SchemaPath;
	Args->TryGetStringField(TEXT("asset_name"), AssetName);
	Args->TryGetStringField(TEXT("save_path"), SavePath);
	if (SavePath.IsEmpty()) SavePath = FSettingsManager::GetDefaultSavePath();
	Args->TryGetStringField(TEXT("schema_path"), SchemaPath);
	HandleCreatePoseSearchDatabase(AssetName, SavePath, SchemaPath, OutJson, OutError);
}

void HandleBuildPoseSearchDatabaseFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJson, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString DatabasePath;
	Args->TryGetStringField(TEXT("database_path"), DatabasePath);
	HandleBuildPoseSearchDatabase(DatabasePath, OutJson, OutError);
}

void HandleGetPoseSearchSummaryFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJson, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString AssetPath;
	Args->TryGetStringField(TEXT("asset_path"), AssetPath);
	HandleGetPoseSearchSummary(AssetPath, OutJson, OutError);
}

void HandleSetPoseSearchChannelPropertyFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJson, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString SchemaPath, PropertyName, PropertyValue;
	int32 ChannelIndex = 0;
	Args->TryGetStringField(TEXT("schema_path"), SchemaPath);
	Args->TryGetNumberField(TEXT("channel_index"), ChannelIndex);
	Args->TryGetStringField(TEXT("property_name"), PropertyName);
	Args->TryGetStringField(TEXT("property_value"), PropertyValue);
	HandleSetPoseSearchChannelProperty(SchemaPath, ChannelIndex, PropertyName, PropertyValue, OutJson, OutError);
}

void HandleSetPoseSearchSchemaSkeletonFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJson, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString SchemaPath, SkeletonPath, Role;
	int32 SkeletonIndex = -1;
	Args->TryGetStringField(TEXT("schema_path"), SchemaPath);
	Args->TryGetStringField(TEXT("skeleton_path"), SkeletonPath);
	Args->TryGetNumberField(TEXT("skeleton_index"), SkeletonIndex);
	Args->TryGetStringField(TEXT("role"), Role);
	HandleSetPoseSearchSchemaSkeleton(SchemaPath, SkeletonPath, SkeletonIndex, Role, OutJson, OutError);
}

void HandleRemoveAnimationFromDatabaseFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJson, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString DatabasePath;
	int32 AnimationIndex = 0;
	Args->TryGetStringField(TEXT("database_path"), DatabasePath);
	Args->TryGetNumberField(TEXT("animation_index"), AnimationIndex);
	HandleRemoveAnimationFromDatabase(DatabasePath, AnimationIndex, OutJson, OutError);
}

void HandleSetPoseSearchSchemaPropertyFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJson, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString SchemaPath, PropertyName, PropertyValue;
	Args->TryGetStringField(TEXT("schema_path"), SchemaPath);
	Args->TryGetStringField(TEXT("property_name"), PropertyName);
	Args->TryGetStringField(TEXT("property_value"), PropertyValue);
	HandleSetPoseSearchSchemaProperty(SchemaPath, PropertyName, PropertyValue, OutJson, OutError);
}

void HandleRemovePoseSearchChannelFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJson, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString SchemaPath;
	int32 ChannelIndex = 0;
	Args->TryGetStringField(TEXT("schema_path"), SchemaPath);
	Args->TryGetNumberField(TEXT("channel_index"), ChannelIndex);
	HandleRemovePoseSearchChannel(SchemaPath, ChannelIndex, OutJson, OutError);
}

void HandleSetAnimationDatabaseEntryPropertyFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJson, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString DatabasePath, PropertyName, PropertyValue;
	int32 AnimationIndex = 0;
	Args->TryGetStringField(TEXT("database_path"), DatabasePath);
	Args->TryGetNumberField(TEXT("animation_index"), AnimationIndex);
	Args->TryGetStringField(TEXT("property_name"), PropertyName);
	Args->TryGetStringField(TEXT("property_value"), PropertyValue);
	HandleSetAnimationDatabaseEntryProperty(DatabasePath, AnimationIndex, PropertyName, PropertyValue, OutJson, OutError);
}

void HandleSetPoseSearchDatabaseSchemaFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJson, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString DatabasePath, SchemaPath;
	Args->TryGetStringField(TEXT("database_path"), DatabasePath);
	Args->TryGetStringField(TEXT("schema_path"), SchemaPath);
	HandleSetPoseSearchDatabaseSchema(DatabasePath, SchemaPath, OutJson, OutError);
}

void HandleGetPoseSearchChannelPropertiesFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJson, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString SchemaPath;
	int32 ChannelIndex = 0;
	Args->TryGetStringField(TEXT("schema_path"), SchemaPath);
	Args->TryGetNumberField(TEXT("channel_index"), ChannelIndex);
	HandleGetPoseSearchChannelProperties(SchemaPath, ChannelIndex, OutJson, OutError);
}

void HandleCreatePoseSearchNormalizationSetFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJson, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString AssetName = TEXT("PS_NormSet"), SavePath;
	Args->TryGetStringField(TEXT("asset_name"), AssetName);
	Args->TryGetStringField(TEXT("save_path"), SavePath);
	if (SavePath.IsEmpty()) SavePath = FSettingsManager::GetDefaultSavePath();
	HandleCreatePoseSearchNormalizationSet(AssetName, SavePath, OutJson, OutError);
}

void HandleAddDatabaseToNormalizationSetFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJson, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString NormSetPath, DatabasePath;
	Args->TryGetStringField(TEXT("norm_set_path"), NormSetPath);
	Args->TryGetStringField(TEXT("database_path"), DatabasePath);
	HandleAddDatabaseToNormalizationSet(NormSetPath, DatabasePath, OutJson, OutError);
}

void HandleSetSchemaMirrorDataTableFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJson, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString SchemaPath, MirrorTablePath;
	Args->TryGetStringField(TEXT("schema_path"), SchemaPath);
	Args->TryGetStringField(TEXT("mirror_table_path"), MirrorTablePath);
	HandleSetSchemaMirrorDataTable(SchemaPath, MirrorTablePath, OutJson, OutError);
}

void HandleDuplicatePoseSearchChannelFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJson, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString SchemaPath;
	int32 ChannelIndex = 0;
	Args->TryGetStringField(TEXT("schema_path"), SchemaPath);
	Args->TryGetNumberField(TEXT("channel_index"), ChannelIndex);
	HandleDuplicatePoseSearchChannel(SchemaPath, ChannelIndex, OutJson, OutError);
}

void HandleReorderPoseSearchChannelsFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJson, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString SchemaPath;
	int32 FromIndex = 0, ToIndex = 0;
	Args->TryGetStringField(TEXT("schema_path"), SchemaPath);
	Args->TryGetNumberField(TEXT("from_index"), FromIndex);
	Args->TryGetNumberField(TEXT("to_index"), ToIndex);
	HandleReorderPoseSearchChannels(SchemaPath, FromIndex, ToIndex, OutJson, OutError);
}

void HandleListPoseSearchChannelTypesFromArgs(const TSharedPtr<FJsonObject>& ,
	FString& OutJson, FString& OutError)
{
	HandleListPoseSearchChannelTypes(OutJson, OutError);
}

}
