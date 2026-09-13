// Copyright 2026, BlueprintsLab, All rights reserved

#include "Tools/AssetPropertyTools.h"
#include "Tools/BatchToolHelper.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonSerializer.h"
#include "EditorAssetLibrary.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "PhysicalMaterials/PhysicalMaterial.h"
#include "PhysicsEngine/BodySetup.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshSocket.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/SkinnedAssetCommon.h"
#include "Engine/SkeletalMeshSocket.h"
#include "Animation/Skeleton.h"
#include "Animation/MorphTarget.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "Rendering/SkeletalMeshModel.h"
#include "Rendering/SkeletalMeshLODModel.h"
#include "ReferenceSkeleton.h"
#include "ClothingAsset.h"
#include "Engine/Texture2D.h"
#include "Engine/CurveTable.h"
#include "Engine/SCS_Node.h"
#include "Components/PrimitiveComponent.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "EngineUtils.h"
#include "Editor.h"
#include "Misc/PackageName.h"
#include "UObject/UObjectHash.h"
#include "MCPToolsLog.h"

struct FCurveTableModeAccessor : public UCurveTable
{
	void SetRichCurvesMode() { CurveTableMode = ECurveTableMode::RichCurves; }
};

namespace AssetPropertyTools
{

static EPhysicalSurface ParseSurfaceType(const FString& TypeStr)
{
	if (TypeStr.IsEmpty() || TypeStr.Equals(TEXT("Default"), ESearchCase::IgnoreCase))
		return SurfaceType_Default;
	if (TypeStr.IsNumeric())
	{
		int32 Idx = FCString::Atoi(*TypeStr);
		if (Idx == 0) return SurfaceType_Default;
		if (Idx >= 1 && Idx <= 62) return (EPhysicalSurface)Idx;
	}
	if (TypeStr.StartsWith(TEXT("SurfaceType"), ESearchCase::IgnoreCase))
	{
		FString Num = TypeStr.Mid(11);
		if (Num.IsNumeric()) return (EPhysicalSurface)FCString::Atoi(*Num);
	}
	return SurfaceType_Default;
}

static bool ParseBool(const FString& S)
{
	return S.Equals(TEXT("true"), ESearchCase::IgnoreCase)
		|| S.Equals(TEXT("yes"), ESearchCase::IgnoreCase)
		|| S.Equals(TEXT("1"), ESearchCase::IgnoreCase);
}

static TSharedPtr<FJsonObject> SuccessJson()
{
	TSharedPtr<FJsonObject> J = MakeShareable(new FJsonObject);
	J->SetBoolField(TEXT("success"), true);
	return J;
}

static FString SerializeJson(TSharedPtr<FJsonObject> J)
{
	FString Out;
	TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&Out);
	FJsonSerializer::Serialize(J.ToSharedRef(), W);
	return Out;
}

void HandleCreatePhysicalMaterial(const FString& Name, const FString& SavePath, float Friction, float Restitution, float Density, const FString& SurfaceType, FString& OutJsonString, FString& OutError)
{
	FString Folder = SavePath.IsEmpty() ? TEXT("/Game/PhysicalMaterials") : SavePath;
	if (Folder.EndsWith(TEXT("/"))) Folder.RemoveFromEnd(TEXT("/"));
	FString FullPath = Folder / Name;

	UPackage* Pkg = CreatePackage(*FullPath);
	Pkg->FullyLoad();

	UPhysicalMaterial* PhysMat = NewObject<UPhysicalMaterial>(Pkg, *Name, RF_Public | RF_Standalone);
	PhysMat->Friction = Friction;
	PhysMat->Restitution = Restitution;
	PhysMat->Density = Density;
	PhysMat->SurfaceType = ParseSurfaceType(SurfaceType);
	PhysMat->PostEditChange();
	PhysMat->MarkPackageDirty();

	FAssetRegistryModule::AssetCreated(PhysMat);
	UEditorAssetLibrary::SaveAsset(FullPath, false);

	auto J = SuccessJson();
	J->SetStringField(TEXT("asset_path"), FullPath);
	J->SetNumberField(TEXT("friction"), Friction);
	J->SetNumberField(TEXT("restitution"), Restitution);
	OutJsonString = SerializeJson(J);
}

void HandleSetPhysicalMaterialProperties(const FString& PhysMatPath, float Friction, float Restitution, float Density, const FString& SurfaceType, FString& OutJsonString, FString& OutError)
{
	UPhysicalMaterial* PhysMat = Cast<UPhysicalMaterial>(UEditorAssetLibrary::LoadAsset(PhysMatPath));
	if (!PhysMat)
	{
		OutError = FString::Printf(TEXT("Could not load PhysicalMaterial at: %s"), *PhysMatPath);
		return;
	}

	if (Friction >= 0.0f) PhysMat->Friction = Friction;
	if (Restitution >= 0.0f) PhysMat->Restitution = Restitution;
	if (Density > 0.0f) PhysMat->Density = Density;
	if (!SurfaceType.IsEmpty()) PhysMat->SurfaceType = ParseSurfaceType(SurfaceType);

	PhysMat->PostEditChange();
	PhysMat->MarkPackageDirty();

	auto J = SuccessJson();
	J->SetStringField(TEXT("asset_path"), PhysMatPath);
	OutJsonString = SerializeJson(J);
}

void HandleSetStaticMeshProperties(const FString& MeshPath, const FString& EnableNanite, const FString& CollisionComplexity, int32 LodBias, FString& OutJsonString, FString& OutError)
{
	UStaticMesh* Mesh = Cast<UStaticMesh>(UEditorAssetLibrary::LoadAsset(MeshPath));
	if (!Mesh)
	{
		OutError = FString::Printf(TEXT("Could not load StaticMesh at: %s"), *MeshPath);
		return;
	}

	if (!EnableNanite.IsEmpty())
	{
#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 7)
		FMeshNaniteSettings NewSettings = Mesh->GetNaniteSettings();
		NewSettings.bEnabled = ParseBool(EnableNanite);
		Mesh->SetNaniteSettings(NewSettings);
#else
		Mesh->NaniteSettings.bEnabled = ParseBool(EnableNanite);
#endif
	}

	if (!CollisionComplexity.IsEmpty())
	{
		UBodySetup* BS = Mesh->GetBodySetup();
		if (BS)
		{
			if (CollisionComplexity.Equals(TEXT("UseComplexAsSimple"), ESearchCase::IgnoreCase) || CollisionComplexity.Equals(TEXT("ComplexAsSimple"), ESearchCase::IgnoreCase))
				BS->CollisionTraceFlag = CTF_UseComplexAsSimple;
			else if (CollisionComplexity.Equals(TEXT("UseSimpleAsComplex"), ESearchCase::IgnoreCase) || CollisionComplexity.Equals(TEXT("SimpleAsComplex"), ESearchCase::IgnoreCase))
				BS->CollisionTraceFlag = CTF_UseSimpleAsComplex;
			else if (CollisionComplexity.Equals(TEXT("UseSimpleAndComplex"), ESearchCase::IgnoreCase))
				BS->CollisionTraceFlag = CTF_UseSimpleAndComplex;
			else
				BS->CollisionTraceFlag = CTF_UseDefault;
		}
	}

	if (LodBias != -999)
		Mesh->SetMinLOD(LodBias);

	Mesh->PostEditChange();
	Mesh->MarkPackageDirty();

	auto J = SuccessJson();
	J->SetStringField(TEXT("asset_path"), MeshPath);
	OutJsonString = SerializeJson(J);
}

void HandleSetTextureProperties(const FString& TexturePath, const FString& SRGB, const FString& CompressionSettings, const FString& MipGenSettings, FString& OutJsonString, FString& OutError)
{
	UTexture2D* Tex = Cast<UTexture2D>(UEditorAssetLibrary::LoadAsset(TexturePath));
	if (!Tex)
	{
		OutError = FString::Printf(TEXT("Could not load Texture2D at: %s"), *TexturePath);
		return;
	}

	if (!SRGB.IsEmpty())
		Tex->SRGB = ParseBool(SRGB);

	if (!CompressionSettings.IsEmpty())
	{
		if (CompressionSettings.Equals(TEXT("Default"), ESearchCase::IgnoreCase))          Tex->CompressionSettings = TC_Default;
		else if (CompressionSettings.Equals(TEXT("Normalmap"), ESearchCase::IgnoreCase))   Tex->CompressionSettings = TC_Normalmap;
		else if (CompressionSettings.Equals(TEXT("Masks"), ESearchCase::IgnoreCase))       Tex->CompressionSettings = TC_Masks;
		else if (CompressionSettings.Equals(TEXT("Grayscale"), ESearchCase::IgnoreCase))   Tex->CompressionSettings = TC_Grayscale;
		else if (CompressionSettings.Equals(TEXT("Alpha"), ESearchCase::IgnoreCase))       Tex->CompressionSettings = TC_Alpha;
		else if (CompressionSettings.Equals(TEXT("HDR"), ESearchCase::IgnoreCase))         Tex->CompressionSettings = TC_HDR;
		else if (CompressionSettings.Equals(TEXT("BC7"), ESearchCase::IgnoreCase))         Tex->CompressionSettings = TC_BC7;
		else if (CompressionSettings.Equals(TEXT("HDR_Compressed"), ESearchCase::IgnoreCase)) Tex->CompressionSettings = TC_HDR_Compressed;
		else if (CompressionSettings.Equals(TEXT("Displacementmap"), ESearchCase::IgnoreCase)) Tex->CompressionSettings = TC_Displacementmap;
		else if (CompressionSettings.Equals(TEXT("VectorDisplacementmap"), ESearchCase::IgnoreCase)) Tex->CompressionSettings = TC_VectorDisplacementmap;
	}

	if (!MipGenSettings.IsEmpty())
	{
		if (MipGenSettings.Equals(TEXT("FromTextureGroup"), ESearchCase::IgnoreCase))      Tex->MipGenSettings = TMGS_FromTextureGroup;
		else if (MipGenSettings.Equals(TEXT("NoMipmaps"), ESearchCase::IgnoreCase))        Tex->MipGenSettings = TMGS_NoMipmaps;
		else if (MipGenSettings.Equals(TEXT("SimpleAverage"), ESearchCase::IgnoreCase))    Tex->MipGenSettings = TMGS_SimpleAverage;
		else if (MipGenSettings.Equals(TEXT("LeaveExisting"), ESearchCase::IgnoreCase) || MipGenSettings.Equals(TEXT("LeaveExistingMips"), ESearchCase::IgnoreCase)) Tex->MipGenSettings = TMGS_LeaveExistingMips;
		else if (MipGenSettings.Equals(TEXT("Unfiltered"), ESearchCase::IgnoreCase))       Tex->MipGenSettings = TMGS_Unfiltered;
	}

	Tex->PostEditChange();
	Tex->MarkPackageDirty();

	auto J = SuccessJson();
	J->SetStringField(TEXT("asset_path"), TexturePath);
	OutJsonString = SerializeJson(J);
}

void HandleAssignPhysicalMaterial(const FString& TargetPath, const FString& ComponentName, const FString& PhysMatPath, FString& OutJsonString, FString& OutError)
{
	UPhysicalMaterial* PhysMat = Cast<UPhysicalMaterial>(UEditorAssetLibrary::LoadAsset(PhysMatPath));
	if (!PhysMat)
	{
		OutError = FString::Printf(TEXT("Could not load PhysicalMaterial at: %s"), *PhysMatPath);
		return;
	}

	if (TargetPath.StartsWith(TEXT("/Game/")) || TargetPath.StartsWith(TEXT("/Engine/")))
	{
		UBlueprint* BP = Cast<UBlueprint>(UEditorAssetLibrary::LoadAsset(TargetPath));
		if (BP && BP->SimpleConstructionScript)
		{
			for (USCS_Node* Node : BP->SimpleConstructionScript->GetAllNodes())
			{
				if (Node && Node->GetVariableName().ToString().Equals(ComponentName, ESearchCase::IgnoreCase))
				{
					UPrimitiveComponent* PC = Cast<UPrimitiveComponent>(Node->ComponentTemplate);
					if (!PC)
					{
						OutError = FString::Printf(TEXT("Component '%s' is not a PrimitiveComponent"), *ComponentName);
						return;
					}
					PC->SetPhysMaterialOverride(PhysMat);
					FBlueprintEditorUtils::MarkBlueprintAsModified(BP);

					auto J = SuccessJson();
					J->SetStringField(TEXT("target"), TargetPath);
					J->SetStringField(TEXT("component"), ComponentName);
					J->SetStringField(TEXT("phys_mat"), PhysMatPath);
					OutJsonString = SerializeJson(J);
					return;
				}
			}
			OutError = FString::Printf(TEXT("Component '%s' not found in Blueprint '%s'"), *ComponentName, *TargetPath);
			return;
		}
	}

	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (World)
	{
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			if (It->GetActorLabel().Equals(TargetPath, ESearchCase::IgnoreCase))
			{
				TArray<UActorComponent*> Comps;
				It->GetComponents(Comps);
				for (UActorComponent* C : Comps)
				{
					if (C->GetName().Equals(ComponentName, ESearchCase::IgnoreCase))
					{
						UPrimitiveComponent* PC = Cast<UPrimitiveComponent>(C);
						if (!PC)
						{
							OutError = FString::Printf(TEXT("Component '%s' is not a PrimitiveComponent"), *ComponentName);
							return;
						}
						PC->SetPhysMaterialOverride(PhysMat);
						It->MarkPackageDirty();

						auto J = SuccessJson();
						J->SetStringField(TEXT("target"), TargetPath);
						J->SetStringField(TEXT("component"), ComponentName);
						J->SetStringField(TEXT("phys_mat"), PhysMatPath);
						OutJsonString = SerializeJson(J);
						return;
					}
				}
				OutError = FString::Printf(TEXT("Component '%s' not found on actor '%s'"), *ComponentName, *TargetPath);
				return;
			}
		}
	}
	OutError = FString::Printf(TEXT("Target '%s' not found as Blueprint or level actor"), *TargetPath);
}

void HandleCreateCurveTable(const FString& Name, const FString& SavePath, FString& OutJsonString, FString& OutError)
{
	FString Folder = SavePath.IsEmpty() ? TEXT("/Game/Data") : SavePath;
	if (Folder.EndsWith(TEXT("/"))) Folder.RemoveFromEnd(TEXT("/"));
	FString FullPath = Folder / Name;

	UPackage* Pkg = CreatePackage(*FullPath);
	Pkg->FullyLoad();

	UCurveTable* CT = NewObject<UCurveTable>(Pkg, *Name, RF_Public | RF_Standalone);

	static_cast<FCurveTableModeAccessor*>(CT)->SetRichCurvesMode();

	CT->MarkPackageDirty();
	FAssetRegistryModule::AssetCreated(CT);
	UEditorAssetLibrary::SaveAsset(FullPath, false);

	auto J = SuccessJson();
	J->SetStringField(TEXT("asset_path"), FullPath);
	OutJsonString = SerializeJson(J);
}

void HandleAddCurveTableRow(const FString& TablePath, const FString& RowName,
	const TArray<float>& Times, const TArray<float>& Values, FString& OutJsonString, FString& OutError)
{
	UCurveTable* CT = Cast<UCurveTable>(UEditorAssetLibrary::LoadAsset(TablePath));
	if (!CT) { OutError = FString::Printf(TEXT("Could not load CurveTable at: %s"), *TablePath); return; }

	if (CT->GetCurveTableMode() != ECurveTableMode::RichCurves)
	{
		OutError = FString::Printf(TEXT("CurveTable '%s' is not in RichCurves mode"), *TablePath);
		return;
	}

	TMap<FName, FRealCurve*>& RowMap = const_cast<TMap<FName, FRealCurve*>&>(CT->GetRowMap());
	FRichCurve* Curve = nullptr;

	FRealCurve** Existing = RowMap.Find(FName(*RowName));
	if (Existing && *Existing)
	{
		Curve = static_cast<FRichCurve*>(*Existing);
	}
	else
	{
		Curve = new FRichCurve();
		RowMap.Add(FName(*RowName), Curve);
	}

	int32 NumKeys = FMath::Min(Times.Num(), Values.Num());
	for (int32 i = 0; i < NumKeys; ++i)
		Curve->AddKey(Times[i], Values[i]);

	CT->PostEditChange();
	CT->MarkPackageDirty();

	auto J = SuccessJson();
	J->SetStringField(TEXT("asset_path"), TablePath);
	J->SetStringField(TEXT("row_name"), RowName);
	J->SetNumberField(TEXT("key_count"), NumKeys);
	OutJsonString = SerializeJson(J);
}

void HandleAddMeshSocket(const FString& MeshPath, const FString& SocketName,
	const FVector& Location, const FRotator& Rotation, const FVector& Scale,
	const FString& BoneName, FString& OutJsonString, FString& OutError)
{
	if (SocketName.IsEmpty()) { OutError = TEXT("socket_name is required"); return; }

	UObject* Asset = UEditorAssetLibrary::LoadAsset(MeshPath);

	if (UStaticMesh* SM = Cast<UStaticMesh>(Asset))
	{
		if (UStaticMeshSocket* Existing = SM->FindSocket(FName(*SocketName)))
			SM->RemoveSocket(Existing);

		UStaticMeshSocket* S = NewObject<UStaticMeshSocket>(SM);
		S->SocketName       = FName(*SocketName);
		S->RelativeLocation = Location;
		S->RelativeRotation = Rotation;
		S->RelativeScale    = Scale;
		SM->AddSocket(S);
		SM->PostEditChange();
		SM->MarkPackageDirty();

		auto J = SuccessJson();
		J->SetStringField(TEXT("mesh_path"),   MeshPath);
		J->SetStringField(TEXT("socket_name"), SocketName);
		J->SetStringField(TEXT("mesh_type"),   TEXT("StaticMesh"));
		OutJsonString = SerializeJson(J);
		return;
	}

	if (USkeletalMesh* SK = Cast<USkeletalMesh>(Asset))
	{
		USkeleton* Skel = SK->GetSkeleton();
		if (!Skel) { OutError = TEXT("SkeletalMesh has no associated Skeleton asset"); return; }

		FName BoneNameToUse = BoneName.IsEmpty() ? FName("root") : FName(*BoneName);

		auto& Sockets = Skel->Sockets;
		for (int32 i = Sockets.Num() - 1; i >= 0; --i)
			if (Sockets[i] && Sockets[i]->SocketName == FName(*SocketName))
				Sockets.RemoveAt(i);

		USkeletalMeshSocket* S = NewObject<USkeletalMeshSocket>(Skel);
		S->SocketName       = FName(*SocketName);
		S->BoneName         = BoneNameToUse;
		S->RelativeLocation = Location;
		S->RelativeRotation = Rotation;
		S->RelativeScale    = Scale;
		Sockets.Add(S);
		Skel->PostEditChange();
		Skel->MarkPackageDirty();

		auto J = SuccessJson();
		J->SetStringField(TEXT("mesh_path"),     MeshPath);
		J->SetStringField(TEXT("socket_name"),   SocketName);
		J->SetStringField(TEXT("bone_name"),     BoneNameToUse.ToString());
		J->SetStringField(TEXT("mesh_type"),     TEXT("SkeletalMesh"));
		J->SetStringField(TEXT("skeleton_path"), Skel->GetPathName());
		OutJsonString = SerializeJson(J);
		return;
	}

	OutError = FString::Printf(TEXT("Could not load StaticMesh or SkeletalMesh at: %s"), *MeshPath);
}

void HandleRemoveMeshSocket(const FString& MeshPath, const FString& SocketName,
	FString& OutJsonString, FString& OutError)
{
	UObject* Asset = UEditorAssetLibrary::LoadAsset(MeshPath);

	if (UStaticMesh* SM = Cast<UStaticMesh>(Asset))
	{
		UStaticMeshSocket* S = SM->FindSocket(FName(*SocketName));
		if (!S) { OutError = FString::Printf(TEXT("Socket '%s' not found on '%s'"), *SocketName, *MeshPath); return; }
		SM->RemoveSocket(S);
		SM->PostEditChange();
		SM->MarkPackageDirty();
	}
	else if (USkeletalMesh* SK = Cast<USkeletalMesh>(Asset))
	{
		USkeleton* Skel = SK->GetSkeleton();
		if (!Skel) { OutError = TEXT("SkeletalMesh has no associated Skeleton asset"); return; }
		auto& Sockets = Skel->Sockets;
		int32 Idx = Sockets.IndexOfByPredicate([&](const TObjectPtr<USkeletalMeshSocket>& S){ return S && S->SocketName == FName(*SocketName); });
		if (Idx == INDEX_NONE) { OutError = FString::Printf(TEXT("Socket '%s' not found on skeleton for '%s'"), *SocketName, *MeshPath); return; }
		Sockets.RemoveAt(Idx);
		Skel->PostEditChange();
		Skel->MarkPackageDirty();
	}
	else
	{
		OutError = FString::Printf(TEXT("Could not load StaticMesh or SkeletalMesh at: %s"), *MeshPath);
		return;
	}

	auto J = SuccessJson();
	J->SetStringField(TEXT("mesh_path"),   MeshPath);
	J->SetStringField(TEXT("socket_name"), SocketName);
	OutJsonString = SerializeJson(J);
}

void HandleListMeshSockets(const FString& MeshPath, FString& OutJsonString, FString& OutError)
{
	UObject* Asset = UEditorAssetLibrary::LoadAsset(MeshPath);

	TArray<TSharedPtr<FJsonValue>> SocketArr;
	auto AddSocket = [&](FName InName, FName InBone, FVector InLoc, FRotator InRot, FVector InScl)
	{
		TSharedPtr<FJsonObject> E = MakeShareable(new FJsonObject);
		E->SetStringField(TEXT("name"), InName.ToString());
		if (!InBone.IsNone()) E->SetStringField(TEXT("bone_name"), InBone.ToString());
		TArray<TSharedPtr<FJsonValue>> Loc, Rot, Scl;
		Loc.Add(MakeShareable(new FJsonValueNumber(InLoc.X)));
		Loc.Add(MakeShareable(new FJsonValueNumber(InLoc.Y)));
		Loc.Add(MakeShareable(new FJsonValueNumber(InLoc.Z)));
		Rot.Add(MakeShareable(new FJsonValueNumber(InRot.Pitch)));
		Rot.Add(MakeShareable(new FJsonValueNumber(InRot.Yaw)));
		Rot.Add(MakeShareable(new FJsonValueNumber(InRot.Roll)));
		Scl.Add(MakeShareable(new FJsonValueNumber(InScl.X)));
		Scl.Add(MakeShareable(new FJsonValueNumber(InScl.Y)));
		Scl.Add(MakeShareable(new FJsonValueNumber(InScl.Z)));
		E->SetArrayField(TEXT("location"), Loc);
		E->SetArrayField(TEXT("rotation"), Rot);
		E->SetArrayField(TEXT("scale"),    Scl);
		SocketArr.Add(MakeShareable(new FJsonValueObject(E)));
	};

	FString MeshType;
	if (UStaticMesh* SM = Cast<UStaticMesh>(Asset))
	{
		MeshType = TEXT("StaticMesh");
		for (UStaticMeshSocket* S : SM->Sockets)
			if (S) AddSocket(S->SocketName, NAME_None, S->RelativeLocation, S->RelativeRotation, S->RelativeScale);
	}
	else if (USkeletalMesh* SK = Cast<USkeletalMesh>(Asset))
	{
		MeshType = TEXT("SkeletalMesh");
		if (USkeleton* Skel = SK->GetSkeleton())
			for (USkeletalMeshSocket* S : Skel->Sockets)
				if (S) AddSocket(S->SocketName, S->BoneName, S->RelativeLocation, S->RelativeRotation, S->RelativeScale);
	}
	else
	{
		OutError = FString::Printf(TEXT("Could not load StaticMesh or SkeletalMesh at: %s"), *MeshPath);
		return;
	}

	auto J = SuccessJson();
	J->SetStringField(TEXT("mesh_path"),  MeshPath);
	J->SetStringField(TEXT("mesh_type"),  MeshType);
	J->SetNumberField(TEXT("count"),      SocketArr.Num());
	J->SetArrayField (TEXT("sockets"),    SocketArr);
	OutJsonString = SerializeJson(J);
}

void HandleListSkeletonBones(const FString& MeshPath, FString& OutJsonString, FString& OutError)
{
	USkeleton* Skel = LoadObject<USkeleton>(nullptr, *MeshPath);
	if (!Skel)
	{
		USkeletalMesh* SK = LoadObject<USkeletalMesh>(nullptr, *MeshPath);
		if (SK) Skel = SK->GetSkeleton();
	}
	if (!Skel)
	{
		OutError = FString::Printf(TEXT("Could not find a Skeleton or SkeletalMesh at: %s. "
			"If this is a Content Browser folder, pass a specific SkeletalMesh asset path instead "
			"(e.g. the `mesh` field from get_blueprint_skeleton, or use list_assets to find the mesh asset)."), *MeshPath);
		return;
	}

	const FReferenceSkeleton& RefSkel = Skel->GetReferenceSkeleton();
	int32 BoneCount = RefSkel.GetNum();

	TArray<TSharedPtr<FJsonValue>> BoneArr;
	for (int32 i = 0; i < BoneCount; ++i)
	{
		FName BoneName = RefSkel.GetBoneName(i);
		int32 ParentIdx = RefSkel.GetParentIndex(i);
		FString ParentName = (ParentIdx >= 0) ? RefSkel.GetBoneName(ParentIdx).ToString() : TEXT("none");

		TSharedPtr<FJsonObject> E = MakeShareable(new FJsonObject);
		E->SetNumberField(TEXT("index"),        i);
		E->SetStringField(TEXT("name"),         BoneName.ToString());
		E->SetNumberField(TEXT("parent_index"), ParentIdx);
		E->SetStringField(TEXT("parent_name"),  ParentName);
		BoneArr.Add(MakeShareable(new FJsonValueObject(E)));
	}

	auto J = SuccessJson();
	J->SetStringField(TEXT("mesh_path"),  MeshPath);
	J->SetNumberField(TEXT("bone_count"), BoneCount);
	J->SetArrayField (TEXT("bones"),      BoneArr);
	OutJsonString = SerializeJson(J);
}

void HandleGetStaticMeshInfo(const FString& MeshPath, FString& OutJsonString, FString& OutError)
{
	UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, *MeshPath);
	if (!Mesh) { OutError = FString::Printf(TEXT("Failed to load StaticMesh: %s"), *MeshPath); return; }

	auto J = SuccessJson();
	J->SetStringField(TEXT("mesh_path"), MeshPath);
	J->SetNumberField(TEXT("num_lods"), Mesh->GetNumLODs());

	TArray<TSharedPtr<FJsonValue>> LodArr;
	for (int32 i = 0; i < Mesh->GetNumLODs(); ++i)
	{
		TSharedPtr<FJsonObject> L = MakeShareable(new FJsonObject);
		L->SetNumberField(TEXT("lod_index"), i);
		if (Mesh->GetRenderData() && Mesh->GetRenderData()->LODResources.IsValidIndex(i))
		{
			const FStaticMeshLODResources& Res = Mesh->GetRenderData()->LODResources[i];
			L->SetNumberField(TEXT("num_triangles"), Res.GetNumTriangles());
			L->SetNumberField(TEXT("num_vertices"), Res.GetNumVertices());
		}
		LodArr.Add(MakeShareable(new FJsonValueObject(L)));
	}
	J->SetArrayField(TEXT("lods"), LodArr);

	FBoxSphereBounds Bounds = Mesh->GetBounds();
	J->SetStringField(TEXT("bounds_origin"), FString::Printf(TEXT("%.1f,%.1f,%.1f"), Bounds.Origin.X, Bounds.Origin.Y, Bounds.Origin.Z));
	J->SetStringField(TEXT("bounds_extent"), FString::Printf(TEXT("%.1f,%.1f,%.1f"), Bounds.BoxExtent.X, Bounds.BoxExtent.Y, Bounds.BoxExtent.Z));

	TArray<TSharedPtr<FJsonValue>> MatArr;
	for (const FStaticMaterial& Mat : Mesh->GetStaticMaterials())
	{
		TSharedPtr<FJsonObject> M = MakeShareable(new FJsonObject);
		M->SetStringField(TEXT("slot_name"), Mat.MaterialSlotName.ToString());
		M->SetStringField(TEXT("material"), Mat.MaterialInterface ? Mat.MaterialInterface->GetPathName() : TEXT("None"));
		MatArr.Add(MakeShareable(new FJsonValueObject(M)));
	}
	J->SetArrayField(TEXT("material_slots"), MatArr);
	J->SetNumberField(TEXT("num_material_slots"), MatArr.Num());

	J->SetNumberField(TEXT("num_sockets"), Mesh->Sockets.Num());

	OutJsonString = SerializeJson(J);
}

void HandleGetTextureInfo(const FString& TexturePath, FString& OutJsonString, FString& OutError)
{
	UTexture2D* Tex = LoadObject<UTexture2D>(nullptr, *TexturePath);
	if (!Tex) { OutError = FString::Printf(TEXT("Failed to load Texture2D: %s"), *TexturePath); return; }

	auto J = SuccessJson();
	J->SetStringField(TEXT("texture_path"), TexturePath);
	J->SetNumberField(TEXT("width"), Tex->GetSizeX());
	J->SetNumberField(TEXT("height"), Tex->GetSizeY());
	J->SetNumberField(TEXT("num_mips"), Tex->GetNumMips());
	J->SetBoolField(TEXT("srgb"), Tex->SRGB);
	J->SetStringField(TEXT("compression_settings"), UEnum::GetValueAsString(Tex->CompressionSettings));
	J->SetStringField(TEXT("pixel_format"), GPixelFormats[Tex->GetPixelFormat()].Name);
	J->SetBoolField(TEXT("has_alpha_channel"), Tex->HasAlphaChannel());

	OutJsonString = SerializeJson(J);
}

void HandleListMorphTargets(const FString& MeshPath, FString& OutJsonString, FString& OutError)
{
	USkeletalMesh* SkelMesh = LoadObject<USkeletalMesh>(nullptr, *MeshPath);
	if (!SkelMesh) { OutError = FString::Printf(TEXT("Failed to load SkeletalMesh: %s"), *MeshPath); return; }

	TArray<TSharedPtr<FJsonValue>> MorphArr;
	for (const TObjectPtr<UMorphTarget>& MT : SkelMesh->GetMorphTargets())
	{
		if (!MT) continue;
		int32 VertCount = 0;
		if (MT->GetMorphLODModels().Num() > 0)
			VertCount = MT->GetMorphLODModels()[0].NumVertices;
		TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
		Obj->SetStringField(TEXT("name"), MT->GetName());
		Obj->SetNumberField(TEXT("lod0_vertex_count"), VertCount);
		MorphArr.Add(MakeShared<FJsonValueObject>(Obj));
	}

	auto J = SuccessJson();
	J->SetStringField(TEXT("mesh_path"), MeshPath);
	J->SetNumberField(TEXT("morph_target_count"), SkelMesh->GetMorphTargets().Num());
	J->SetArrayField(TEXT("morph_targets"), MorphArr);
	OutJsonString = SerializeJson(J);
}

void HandleGetSkeletalMeshInfo(const FString& MeshPath, FString& OutJsonString, FString& OutError)
{
	USkeletalMesh* SkelMesh = LoadObject<USkeletalMesh>(nullptr, *MeshPath);
	if (!SkelMesh) { OutError = FString::Printf(TEXT("Failed to load SkeletalMesh: %s"), *MeshPath); return; }

	TArray<TSharedPtr<FJsonValue>> LODArr;
	const int32 NumLODs = SkelMesh->GetLODNum();
	for (int32 i = 0; i < NumLODs; i++)
	{
		TSharedPtr<FJsonObject> LOD = MakeShared<FJsonObject>();
		LOD->SetNumberField(TEXT("lod_index"), i);
		if (SkelMesh->GetResourceForRendering() && SkelMesh->GetResourceForRendering()->LODRenderData.IsValidIndex(i))
		{
			const FSkeletalMeshLODRenderData& RenderData = SkelMesh->GetResourceForRendering()->LODRenderData[i];
			int32 TriCount = 0;
			for (int32 s = 0; s < RenderData.RenderSections.Num(); s++)
				TriCount += RenderData.RenderSections[s].NumTriangles;
			LOD->SetNumberField(TEXT("triangle_count"), TriCount);
			LOD->SetNumberField(TEXT("section_count"), RenderData.RenderSections.Num());
		}
		LODArr.Add(MakeShared<FJsonValueObject>(LOD));
	}

	int32 BoneCount = SkelMesh->GetRefSkeleton().GetNum();

	TArray<TSharedPtr<FJsonValue>> MatArr;
	for (const FSkeletalMaterial& Mat : SkelMesh->GetMaterials())
	{
		TSharedPtr<FJsonObject> M = MakeShared<FJsonObject>();
		M->SetStringField(TEXT("slot_name"), Mat.MaterialSlotName.ToString());
		M->SetStringField(TEXT("material"), Mat.MaterialInterface ? Mat.MaterialInterface->GetPathName() : TEXT("None"));
		MatArr.Add(MakeShared<FJsonValueObject>(M));
	}

	FString PhysAssetName = TEXT("None");
	if (UPhysicsAsset* PA = SkelMesh->GetPhysicsAsset())
		PhysAssetName = PA->GetPathName();

	const TArray<UClothingAssetBase*>& Clothing = SkelMesh->GetMeshClothingAssets();

	auto J = SuccessJson();
	J->SetStringField(TEXT("mesh_path"), MeshPath);
	J->SetNumberField(TEXT("lod_count"), NumLODs);
	J->SetNumberField(TEXT("bone_count"), BoneCount);
	J->SetNumberField(TEXT("morph_target_count"), SkelMesh->GetMorphTargets().Num());
	J->SetNumberField(TEXT("material_count"), SkelMesh->GetMaterials().Num());
	J->SetNumberField(TEXT("clothing_asset_count"), Clothing.Num());
	J->SetStringField(TEXT("physics_asset"), PhysAssetName);
	J->SetArrayField(TEXT("lods"), LODArr);
	J->SetArrayField(TEXT("materials"), MatArr);
	OutJsonString = SerializeJson(J);
}

void HandleAssignPhysicsAssetToSkeletalMesh(const FString& MeshPath, const FString& PhysicsAssetPath,
	FString& OutJsonString, FString& OutError)
{

	USkeletalMesh* SkelMesh = LoadObject<USkeletalMesh>(nullptr, *MeshPath);
	if (!SkelMesh) { OutError = FString::Printf(TEXT("Failed to load SkeletalMesh: %s"), *MeshPath); return; }

	UPhysicsAsset* PA = LoadObject<UPhysicsAsset>(nullptr, *PhysicsAssetPath);
	if (!PA) { OutError = FString::Printf(TEXT("Failed to load PhysicsAsset: %s"), *PhysicsAssetPath); return; }

	SkelMesh->SetPhysicsAsset(PA);
	SkelMesh->MarkPackageDirty();
	UEditorAssetLibrary::SaveAsset(MeshPath, false);

	auto J = SuccessJson();
	J->SetStringField(TEXT("mesh_path"), MeshPath);
	J->SetStringField(TEXT("physics_asset"), PA->GetPathName());
	OutJsonString = SerializeJson(J);
}

void HandleListClothingAssets(const FString& MeshPath, FString& OutJsonString, FString& OutError)
{

	USkeletalMesh* SkelMesh = LoadObject<USkeletalMesh>(nullptr, *MeshPath);
	if (!SkelMesh) { OutError = FString::Printf(TEXT("Failed to load SkeletalMesh: %s"), *MeshPath); return; }

	const TArray<UClothingAssetBase*>& Clothing = SkelMesh->GetMeshClothingAssets();
	TArray<TSharedPtr<FJsonValue>> ClothArr;
	for (int32 i = 0; i < Clothing.Num(); i++)
	{
		UClothingAssetBase* Asset = Clothing[i];
		TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
		Obj->SetNumberField(TEXT("index"), i);
		Obj->SetStringField(TEXT("name"), Asset ? Asset->GetName() : TEXT("None"));
		Obj->SetStringField(TEXT("path"), Asset ? Asset->GetPathName() : TEXT("None"));
		ClothArr.Add(MakeShared<FJsonValueObject>(Obj));
	}

	auto J = SuccessJson();
	J->SetStringField(TEXT("mesh_path"), MeshPath);
	J->SetNumberField(TEXT("clothing_asset_count"), Clothing.Num());
	J->SetArrayField(TEXT("clothing_assets"), ClothArr);
	OutJsonString = SerializeJson(J);
}

static FString SanitizeMeshPath(const FString& InPath)
{
	FString Path = InPath;
	if (Path.Contains(TEXT("'")))
	{
		int32 Start = Path.Find(TEXT("'"));
		int32 End = Path.Find(TEXT("'"), ESearchCase::IgnoreCase, ESearchDir::FromEnd);
		if (Start != End && Start != INDEX_NONE)
			Path = Path.Mid(Start + 1, End - Start - 1);
	}
	int32 DotIdx;
	if (Path.FindLastChar('.', DotIdx) && DotIdx > 0)
		Path.LeftInline(DotIdx);
	return Path;
}

static void CollectDescendantBones(const FReferenceSkeleton& RefSkel, int32 RootBoneIdx, TSet<int32>& OutBones)
{
	OutBones.Add(RootBoneIdx);
	for (int32 i = RootBoneIdx + 1; i < RefSkel.GetNum(); i++)
	{
		if (OutBones.Contains(RefSkel.GetParentIndex(i)))
			OutBones.Add(i);
	}
}

static TArray<TSet<int32>> BuildBonePartitions(const FReferenceSkeleton& RefSkel,
	const TArray<FString>& BoneNames, TArray<FString>& OutPartitionNames, TArray<FString>& OutSkippedBones)
{
	TSet<int32> ClaimedBones;
	TArray<TSet<int32>> Partitions;

	Partitions.AddDefaulted();
	OutPartitionNames.Add(TEXT("Body"));

	for (const FString& BoneName : BoneNames)
	{
		int32 BoneIdx = RefSkel.FindBoneIndex(FName(*BoneName));
		if (BoneIdx == INDEX_NONE)
		{
			OutSkippedBones.Add(BoneName);
			continue;
		}
		TSet<int32> Descendants;
		CollectDescendantBones(RefSkel, BoneIdx, Descendants);

		TSet<int32> Unique = Descendants.Difference(ClaimedBones);
		if (Unique.Num() == 0)
		{
			OutSkippedBones.Add(BoneName);
			continue;
		}

		ClaimedBones.Append(Unique);
		Partitions.Add(Unique);
		OutPartitionNames.Add(BoneName);
	}

	for (int32 i = 0; i < RefSkel.GetNum(); i++)
	{
		if (!ClaimedBones.Contains(i))
			Partitions[0].Add(i);
	}

	return Partitions;
}

static TArray<int32> ClassifyVertices(const FSkeletalMeshLODModel& LODModel,
	const TArray<TSet<int32>>& BonePartitions, float WeightThreshold)
{
	const int32 TotalVerts = LODModel.NumVertices;
	TArray<int32> VertexPartition;
	VertexPartition.SetNum(TotalVerts);

	for (const FSkelMeshSection& Section : LODModel.Sections)
	{
		for (int32 vi = 0; vi < Section.SoftVertices.Num(); vi++)
		{
			const FSoftSkinVertex& V = Section.SoftVertices[vi];
			const int32 GlobalVertIdx = Section.BaseVertexIndex + vi;

			TArray<uint32> PartitionWeights;
			PartitionWeights.SetNumZeroed(BonePartitions.Num());

			for (int32 inf = 0; inf < MAX_TOTAL_INFLUENCES; inf++)
			{
				if (V.InfluenceWeights[inf] == 0) continue;
				const int32 LocalBoneIdx = V.InfluenceBones[inf];
				if (!Section.BoneMap.IsValidIndex(LocalBoneIdx)) continue;
				const int32 GlobalBoneIdx = Section.BoneMap[LocalBoneIdx];

				for (int32 p = 0; p < BonePartitions.Num(); p++)
				{
					if (BonePartitions[p].Contains(GlobalBoneIdx))
					{
						PartitionWeights[p] += V.InfluenceWeights[inf];
						break;
					}
				}
			}

			int32 BestPartition = 0;
			uint32 BestWeight = 0;
			for (int32 p = 0; p < PartitionWeights.Num(); p++)
			{
				if (PartitionWeights[p] > BestWeight)
				{
					BestWeight = PartitionWeights[p];
					BestPartition = p;
				}
			}
			VertexPartition[GlobalVertIdx] = BestPartition;
		}
	}

	return VertexPartition;
}

static FReferenceSkeleton CreateSubsetSkeleton(const FReferenceSkeleton& SrcRefSkel, const TSet<int32>& KeepBones)
{
	TSet<int32> AllNeeded = KeepBones;
	for (int32 BoneIdx : KeepBones)
	{
		int32 Parent = SrcRefSkel.GetParentIndex(BoneIdx);
		while (Parent != INDEX_NONE)
		{
			if (AllNeeded.Contains(Parent)) break;
			AllNeeded.Add(Parent);
			Parent = SrcRefSkel.GetParentIndex(Parent);
		}
	}

	TArray<int32> SortedBones = AllNeeded.Array();
	SortedBones.Sort();

	TMap<int32, int32> OldToNew;
	for (int32 NewIdx = 0; NewIdx < SortedBones.Num(); NewIdx++)
		OldToNew.Add(SortedBones[NewIdx], NewIdx);

	FReferenceSkeleton NewRefSkel;
	FReferenceSkeletonModifier Modifier(NewRefSkel, nullptr);

	for (int32 NewIdx = 0; NewIdx < SortedBones.Num(); NewIdx++)
	{
		const int32 OldIdx = SortedBones[NewIdx];
		FMeshBoneInfo BoneInfo = SrcRefSkel.GetRefBoneInfo()[OldIdx];

		const int32 OldParent = BoneInfo.ParentIndex;
		if (OldParent == INDEX_NONE)
			BoneInfo.ParentIndex = INDEX_NONE;
		else
			BoneInfo.ParentIndex = OldToNew.FindChecked(OldParent);

		Modifier.Add(BoneInfo, SrcRefSkel.GetRefBonePose()[OldIdx]);
	}

	return NewRefSkel;
}

static void CountPartitionGeometry(const FSkeletalMeshLODModel& LODModel,
	const TArray<int32>& VertexPartition, int32 PartitionIdx,
	int32& OutVertCount, int32& OutTriCount)
{
	TSet<int32> UsedVerts;
	for (int32 i = 0; i < (int32)LODModel.IndexBuffer.Num(); i += 3)
	{
		const int32 I0 = LODModel.IndexBuffer[i];
		const int32 I1 = LODModel.IndexBuffer[i + 1];
		const int32 I2 = LODModel.IndexBuffer[i + 2];

		if (VertexPartition[I0] == PartitionIdx &&
			VertexPartition[I1] == PartitionIdx &&
			VertexPartition[I2] == PartitionIdx)
		{
			OutTriCount++;
			UsedVerts.Add(I0);
			UsedVerts.Add(I1);
			UsedVerts.Add(I2);
		}
	}
	OutVertCount = UsedVerts.Num();
}

static int32 CountBoundaryVertices(const FSkeletalMeshLODModel& LODModel,
	const TArray<TSet<int32>>& BonePartitions)
{
	int32 BoundaryCount = 0;
	for (const FSkelMeshSection& Section : LODModel.Sections)
	{
		for (const FSoftSkinVertex& V : Section.SoftVertices)
		{
			TSet<int32> InfluencedPartitions;
			for (int32 inf = 0; inf < MAX_TOTAL_INFLUENCES; inf++)
			{
				if (V.InfluenceWeights[inf] == 0) continue;
				const int32 LocalBoneIdx = V.InfluenceBones[inf];
				if (!Section.BoneMap.IsValidIndex(LocalBoneIdx)) continue;
				const int32 GlobalBoneIdx = Section.BoneMap[LocalBoneIdx];
				for (int32 p = 0; p < BonePartitions.Num(); p++)
				{
					if (BonePartitions[p].Contains(GlobalBoneIdx))
					{
						InfluencedPartitions.Add(p);
						break;
					}
				}
			}
			if (InfluencedPartitions.Num() > 1) BoundaryCount++;
		}
	}
	return BoundaryCount;
}

void HandlePreviewSplitSkeletalMesh(const FString& MeshPath, const TArray<FString>& BoneNames,
	float WeightThreshold, FString& OutJsonString, FString& OutError)
{
	const FString CleanMeshPath = SanitizeMeshPath(MeshPath);

	if (BoneNames.Num() == 0) { OutError = TEXT("bone_names array is empty"); return; }

	USkeletalMesh* SkelMesh = LoadObject<USkeletalMesh>(nullptr, *CleanMeshPath);
	if (!SkelMesh) { OutError = FString::Printf(TEXT("Failed to load SkeletalMesh: %s"), *CleanMeshPath); return; }

	const FReferenceSkeleton& RefSkel = SkelMesh->GetRefSkeleton();
	FSkeletalMeshModel* ImportedModel = SkelMesh->GetImportedModel();
	if (!ImportedModel || ImportedModel->LODModels.Num() == 0)
	{
		OutError = TEXT("SkeletalMesh has no imported model data (editor-only data required)");
		return;
	}

	TArray<FString> PartitionNames;
	TArray<FString> SkippedBones;
	TArray<TSet<int32>> BonePartitions = BuildBonePartitions(RefSkel, BoneNames, PartitionNames, SkippedBones);

	if (BonePartitions.Num() <= 1)
	{
		OutError = TEXT("No valid split bones found — all bone names were invalid or already claimed");
		return;
	}

	const FSkeletalMeshLODModel& LOD0 = ImportedModel->LODModels[0];
	TArray<int32> VertexPartition = ClassifyVertices(LOD0, BonePartitions, WeightThreshold);

	TArray<TSharedPtr<FJsonValue>> PartArr;
	int32 TotalVerts = 0, TotalTris = 0;
	for (int32 p = 0; p < BonePartitions.Num(); p++)
	{
		int32 VertCount = 0, TriCount = 0;
		CountPartitionGeometry(LOD0, VertexPartition, p, VertCount, TriCount);
		TotalVerts += VertCount;
		TotalTris += TriCount;

		TSharedPtr<FJsonObject> P = MakeShared<FJsonObject>();
		P->SetStringField(TEXT("name"), PartitionNames[p]);
		P->SetNumberField(TEXT("vertex_count"), VertCount);
		P->SetNumberField(TEXT("triangle_count"), TriCount);
		P->SetNumberField(TEXT("bone_count"), BonePartitions[p].Num());
		PartArr.Add(MakeShared<FJsonValueObject>(P));
	}

	int32 BoundaryVerts = CountBoundaryVertices(LOD0, BonePartitions);

	auto J = SuccessJson();
	J->SetStringField(TEXT("source_mesh"), CleanMeshPath);
	J->SetNumberField(TEXT("total_vertices"), TotalVerts);
	J->SetNumberField(TEXT("total_triangles"), TotalTris);
	J->SetNumberField(TEXT("boundary_vertices"), BoundaryVerts);
	J->SetNumberField(TEXT("lod_count"), ImportedModel->LODModels.Num());
	J->SetArrayField(TEXT("partitions"), PartArr);
	if (SkippedBones.Num() > 0)
	{
		TArray<TSharedPtr<FJsonValue>> SkipArr;
		for (const FString& S : SkippedBones) SkipArr.Add(MakeShared<FJsonValueString>(S));
		J->SetArrayField(TEXT("skipped_bones"), SkipArr);
	}
	OutJsonString = SerializeJson(J);
}

void HandleSplitSkeletalMesh(const FString& MeshPath, const TArray<FString>& BoneNames,
	const FString& OutputPath, float WeightThreshold,
	const TArray<FString>& UserPartitionNames,
	FString& OutJsonString, FString& OutError)
{
	const FString CleanMeshPath = SanitizeMeshPath(MeshPath);

	if (BoneNames.Num() == 0) { OutError = TEXT("bone_names array is empty"); return; }

	USkeletalMesh* SrcMesh = LoadObject<USkeletalMesh>(nullptr, *CleanMeshPath);
	if (!SrcMesh) { OutError = FString::Printf(TEXT("Failed to load SkeletalMesh: %s"), *CleanMeshPath); return; }

	const FReferenceSkeleton& SrcRefSkel = SrcMesh->GetRefSkeleton();
	FSkeletalMeshModel* SrcModel = SrcMesh->GetImportedModel();
	if (!SrcModel || SrcModel->LODModels.Num() == 0)
	{
		OutError = TEXT("SkeletalMesh has no imported model data (editor-only data required)");
		return;
	}

	FString OutFolder = OutputPath;
	if (OutFolder.IsEmpty())
	{
		FString SrcFolder = FPackageName::GetLongPackagePath(CleanMeshPath);
		FString SrcName = FPackageName::GetShortName(CleanMeshPath);
		OutFolder = SrcFolder / (SrcName + TEXT("_Split"));
	}
	if (OutFolder.EndsWith(TEXT("/"))) OutFolder.RemoveFromEnd(TEXT("/"));

	TArray<FString> PartitionNames;
	TArray<FString> SkippedBones;
	TArray<TSet<int32>> BonePartitions = BuildBonePartitions(SrcRefSkel, BoneNames, PartitionNames, SkippedBones);

	if (BonePartitions.Num() <= 1)
	{
		OutError = TEXT("No valid split bones found — all bone names were invalid or already claimed");
		return;
	}

	if (UserPartitionNames.Num() > 0)
	{
		for (int32 i = 0; i < FMath::Min(UserPartitionNames.Num(), PartitionNames.Num()); i++)
		{
			if (!UserPartitionNames[i].IsEmpty())
				PartitionNames[i] = UserPartitionNames[i];
		}
	}

	USkeleton* SharedSkeleton = SrcMesh->GetSkeleton();
	FString SrcMeshName = FPackageName::GetShortName(CleanMeshPath);

	TArray<TSharedPtr<FJsonValue>> PartArr;

	for (int32 p = 0; p < BonePartitions.Num(); p++)
	{
		const TSet<int32>& PartBones = BonePartitions[p];
		if (PartBones.Num() == 0) continue;

		FString PartName = PartitionNames[p];
		FString SafePartName = PartName;
		SafePartName.ReplaceInline(TEXT(" "), TEXT("_"));
		SafePartName.ReplaceInline(TEXT("."), TEXT("_"));
		SafePartName.ReplaceInline(TEXT(":"), TEXT("_"));
		FString AssetName = SrcMeshName + TEXT("_") + SafePartName;
		FString FullPath = OutFolder / AssetName;

		UPackage* Pkg = CreatePackage(*FullPath);
		if (!Pkg)
		{
			OutError = FString::Printf(TEXT("Failed to create package at path: %s"), *FullPath);
			return;
		}
		Pkg->FullyLoad();

		USkeletalMesh* NewMesh = NewObject<USkeletalMesh>(Pkg, FName(*AssetName), RF_Public | RF_Standalone);

		NewMesh->SetRefSkeleton(SrcRefSkel);
		if (SharedSkeleton)
			NewMesh->SetSkeleton(SharedSkeleton);

		NewMesh->SetMaterials(SrcMesh->GetMaterials());

		FSkeletalMeshModel* NewModel = NewMesh->GetImportedModel();
		if (!NewModel)
		{
			OutError = TEXT("Failed to initialize imported model on new mesh");
			return;
		}
		NewModel->LODModels.Empty();

		int32 TotalNewVerts = 0, TotalNewTris = 0;

		for (int32 LODIdx = 0; LODIdx < SrcModel->LODModels.Num(); LODIdx++)
		{
			const FSkeletalMeshLODModel& SrcLOD = SrcModel->LODModels[LODIdx];
			TArray<int32> VertPartition = ClassifyVertices(SrcLOD, BonePartitions, WeightThreshold);

			FSkeletalMeshLODInfo& LODInfo = NewMesh->AddLODInfo();
			if (LODIdx < SrcMesh->GetLODNum())
				LODInfo = *SrcMesh->GetLODInfo(LODIdx);

			FSkeletalMeshLODModel* NewLOD = new FSkeletalMeshLODModel();
			NewModel->LODModels.Add(NewLOD);

			TArray<uint32> NewIndexBuffer;
			int32 LODTriCount = 0;
			uint32 RunningBaseVertex = 0;

			for (const FSkelMeshSection& SrcSection : SrcLOD.Sections)
			{
				TMap<int32, int32> SrcLocalToNewLocal;
				TArray<FSoftSkinVertex> NewSectionVerts;

				for (int32 vi = 0; vi < SrcSection.SoftVertices.Num(); vi++)
				{
					const int32 GlobalIdx = SrcSection.BaseVertexIndex + vi;
					if (VertPartition[GlobalIdx] == p)
					{
						SrcLocalToNewLocal.Add(vi, NewSectionVerts.Num());
						NewSectionVerts.Add(SrcSection.SoftVertices[vi]);
					}
				}

				if (NewSectionVerts.Num() == 0) continue;

				TArray<uint32> NewSectionIndices;
				for (uint32 ti = 0; ti < SrcSection.NumTriangles; ti++)
				{
					const uint32 BaseIdx = SrcSection.BaseIndex + ti * 3;
					const int32 GI0 = SrcLOD.IndexBuffer[BaseIdx];
					const int32 GI1 = SrcLOD.IndexBuffer[BaseIdx + 1];
					const int32 GI2 = SrcLOD.IndexBuffer[BaseIdx + 2];
					const int32 SL0 = GI0 - SrcSection.BaseVertexIndex;
					const int32 SL1 = GI1 - SrcSection.BaseVertexIndex;
					const int32 SL2 = GI2 - SrcSection.BaseVertexIndex;

					const int32* NL0 = SrcLocalToNewLocal.Find(SL0);
					const int32* NL1 = SrcLocalToNewLocal.Find(SL1);
					const int32* NL2 = SrcLocalToNewLocal.Find(SL2);
					if (NL0 && NL1 && NL2)
					{
						NewSectionIndices.Add(*NL0);
						NewSectionIndices.Add(*NL1);
						NewSectionIndices.Add(*NL2);
					}
				}

				if (NewSectionIndices.Num() == 0) continue;

				FSkelMeshSection NewSection;
				NewSection.MaterialIndex = SrcSection.MaterialIndex;
				NewSection.BaseIndex = NewIndexBuffer.Num();
				NewSection.NumTriangles = NewSectionIndices.Num() / 3;
				NewSection.BaseVertexIndex = RunningBaseVertex;
				NewSection.NumVertices = NewSectionVerts.Num();
				NewSection.SoftVertices = MoveTemp(NewSectionVerts);
				NewSection.BoneMap = SrcSection.BoneMap;
				NewSection.bCastShadow = SrcSection.bCastShadow;
				NewSection.bVisibleInRayTracing = SrcSection.bVisibleInRayTracing;
				NewSection.bRecomputeTangent = SrcSection.bRecomputeTangent;
				NewSection.RecomputeTangentsVertexMaskChannel = SrcSection.RecomputeTangentsVertexMaskChannel;
				NewSection.OriginalDataSectionIndex = NewLOD->Sections.Num();

				for (const uint32& Idx : NewSectionIndices)
					NewIndexBuffer.Add(Idx + RunningBaseVertex);

				RunningBaseVertex += NewSection.NumVertices;
				LODTriCount += NewSection.NumTriangles;

				NewSection.CalcMaxBoneInfluences();
				NewSection.CalcUse16BitBoneIndex();
				NewLOD->Sections.Add(MoveTemp(NewSection));
			}

			NewLOD->IndexBuffer = MoveTemp(NewIndexBuffer);
			NewLOD->NumVertices = RunningBaseVertex;
			NewLOD->NumTexCoords = SrcLOD.NumTexCoords;

			TSet<FBoneIndexType> ActiveBones;
			for (const FSkelMeshSection& Sec : NewLOD->Sections)
				for (const FBoneIndexType& B : Sec.BoneMap)
					ActiveBones.Add(B);
			NewLOD->ActiveBoneIndices = ActiveBones.Array();
			NewLOD->ActiveBoneIndices.Sort();

			TSet<FBoneIndexType> Required(ActiveBones);
			for (FBoneIndexType B : ActiveBones)
			{
				int32 Parent = SrcRefSkel.GetParentIndex(B);
				while (Parent != INDEX_NONE)
				{
					if (Required.Contains((FBoneIndexType)Parent)) break;
					Required.Add((FBoneIndexType)Parent);
					Parent = SrcRefSkel.GetParentIndex(Parent);
				}
			}
			NewLOD->RequiredBones = Required.Array();
			NewLOD->RequiredBones.Sort();

			if (LODIdx == 0)
			{
				TotalNewVerts = RunningBaseVertex;
				TotalNewTris = LODTriCount;
			}
		}

		if (TotalNewVerts == 0)
		{
			Pkg->ClearDirtyFlag();
			continue;
		}

		NewMesh->CalculateInvRefMatrices();
		NewMesh->Build();

		FAssetRegistryModule::AssetCreated(NewMesh);
		NewMesh->MarkPackageDirty();

		TSharedPtr<FJsonObject> PObj = MakeShared<FJsonObject>();
		PObj->SetStringField(TEXT("name"), PartName);
		PObj->SetStringField(TEXT("asset_path"), FullPath);
		PObj->SetNumberField(TEXT("bone_count"), PartBones.Num());
		PObj->SetNumberField(TEXT("vertex_count"), TotalNewVerts);
		PObj->SetNumberField(TEXT("triangle_count"), TotalNewTris);
		PartArr.Add(MakeShared<FJsonValueObject>(PObj));
	}

	auto J = SuccessJson();
	J->SetStringField(TEXT("source_mesh"), CleanMeshPath);

	TArray<TSharedPtr<FJsonValue>> SplitBoneArr;
	for (const FString& B : BoneNames) SplitBoneArr.Add(MakeShared<FJsonValueString>(B));
	J->SetArrayField(TEXT("split_bones"), SplitBoneArr);

	J->SetArrayField(TEXT("partitions"), PartArr);

	if (SharedSkeleton)
		J->SetStringField(TEXT("skeleton"), SharedSkeleton->GetPathName());
	J->SetStringField(TEXT("note"), TEXT("All parts share the original skeleton for Master Pose Component compatibility"));

	if (SkippedBones.Num() > 0)
	{
		TArray<TSharedPtr<FJsonValue>> SkipArr;
		for (const FString& S : SkippedBones) SkipArr.Add(MakeShared<FJsonValueString>(S));
		J->SetArrayField(TEXT("skipped_bones"), SkipArr);
	}

	OutJsonString = SerializeJson(J);
}

void HandleAddMeshSocketFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	FString MeshPath; Args->TryGetStringField(TEXT("mesh_path"), MeshPath);

	auto ParseVec = [](const TSharedPtr<FJsonObject>& Obj, const FString& Key, const FVector& Default) -> FVector
	{
		const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
		if (Obj->TryGetArrayField(Key, Arr) && Arr && Arr->Num() >= 3)
		{ double X=0,Y=0,Z=0; (*Arr)[0]->TryGetNumber(X); (*Arr)[1]->TryGetNumber(Y); (*Arr)[2]->TryGetNumber(Z); return FVector(X,Y,Z); }
		return Default;
	};
	auto ParseRot = [](const TSharedPtr<FJsonObject>& Obj, const FString& Key) -> FRotator
	{
		const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
		if (Obj->TryGetArrayField(Key, Arr) && Arr && Arr->Num() >= 3)
		{ double P=0,Y=0,R=0; (*Arr)[0]->TryGetNumber(P); (*Arr)[1]->TryGetNumber(Y); (*Arr)[2]->TryGetNumber(R); return FRotator(P,Y,R); }
		return FRotator(0,0,0);
	};

	const TArray<TSharedPtr<FJsonValue>>* ItemsArray = nullptr;
	if (BatchToolHelper::TryGetBatchItems(Args, TEXT("sockets"), ItemsArray))
	{
		BatchToolHelper::FBatchResultBuilder Batch;
		for (int32 i = 0; i < ItemsArray->Num(); i++)
		{
			TSharedPtr<FJsonObject> Item = (*ItemsArray)[i]->AsObject();
			if (!Item.IsValid()) { Batch.AddFailure(i, TEXT("Invalid item")); continue; }
			FString SN = BatchToolHelper::GetItemString(Item, TEXT("socket_name"), TEXT("name"));
			if (SN.IsEmpty()) { Batch.AddFailure(i, TEXT("Missing socket_name")); continue; }
			FVector Loc = ParseVec(Item, TEXT("location"), FVector::ZeroVector);
			FRotator Rot = ParseRot(Item, TEXT("rotation"));
			FVector Sc = ParseVec(Item, TEXT("scale"), FVector::OneVector);
			FString BN = BatchToolHelper::GetItemString(Item, TEXT("bone_name"));
			FString ItemOut, ItemErr;
			HandleAddMeshSocket(MeshPath, SN, Loc, Rot, Sc, BN, ItemOut, ItemErr);
			if (ItemErr.IsEmpty())
			{
				auto Extra = MakeShared<FJsonObject>(); Extra->SetStringField(TEXT("socket_name"), SN);
				Batch.AddSuccess(i, Extra);
			}
			else Batch.AddFailure(i, ItemErr);
		}
		Batch.Finalize(OutJsonString);
		return;
	}
	FString SN, BN;
	Args->TryGetStringField(TEXT("socket_name"), SN); Args->TryGetStringField(TEXT("bone_name"), BN);
	FVector Loc = ParseVec(Args, TEXT("location"), FVector::ZeroVector);
	FRotator Rot = ParseRot(Args, TEXT("rotation"));
	FVector Sc = ParseVec(Args, TEXT("scale"), FVector::OneVector);
	HandleAddMeshSocket(MeshPath, SN, Loc, Rot, Sc, BN, OutJsonString, OutError);
}

void HandleRemoveMeshSocketFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	FString MeshPath; Args->TryGetStringField(TEXT("mesh_path"), MeshPath);

	const TArray<TSharedPtr<FJsonValue>>* ItemsArray = nullptr;
	if (BatchToolHelper::TryGetBatchItems(Args, TEXT("sockets"), ItemsArray))
	{
		BatchToolHelper::FBatchResultBuilder Batch;
		for (int32 i = 0; i < ItemsArray->Num(); i++)
		{
			FString SN;
			TSharedPtr<FJsonObject> Item = (*ItemsArray)[i]->AsObject();
			if (Item.IsValid())
				SN = BatchToolHelper::GetItemString(Item, TEXT("socket_name"), TEXT("name"));
			else
				(*ItemsArray)[i]->TryGetString(SN);
			if (SN.IsEmpty()) { Batch.AddFailure(i, TEXT("Missing socket_name")); continue; }
			FString ItemOut, ItemErr;
			HandleRemoveMeshSocket(MeshPath, SN, ItemOut, ItemErr);
			if (ItemErr.IsEmpty()) Batch.AddSuccess(i);
			else Batch.AddFailure(i, ItemErr);
		}
		Batch.Finalize(OutJsonString);
		return;
	}
	FString SN; Args->TryGetStringField(TEXT("socket_name"), SN);
	HandleRemoveMeshSocket(MeshPath, SN, OutJsonString, OutError);
}

void HandleGetClothConfigFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	FString MeshPath;
	Args->TryGetStringField(TEXT("mesh_path"), MeshPath);
	if (MeshPath.IsEmpty()) { OutError = TEXT("mesh_path required"); return; }

	double ClothIdxD = 0;
	Args->TryGetNumberField(TEXT("cloth_index"), ClothIdxD);
	int32 ClothIndex = (int32)ClothIdxD;

	USkeletalMesh* SkelMesh = LoadObject<USkeletalMesh>(nullptr, *MeshPath);
	if (!SkelMesh) { OutError = FString::Printf(TEXT("SkeletalMesh not found: %s"), *MeshPath); return; }

	const TArray<UClothingAssetBase*>& Clothing = SkelMesh->GetMeshClothingAssets();
	if (Clothing.Num() == 0) { OutError = TEXT("SkeletalMesh has no clothing assets. Use list_clothing_assets to check."); return; }
	if (ClothIndex < 0 || ClothIndex >= Clothing.Num())
	{
		OutError = FString::Printf(TEXT("cloth_index %d out of range (count=%d)"), ClothIndex, Clothing.Num());
		return;
	}

	UClothingAssetBase* ClothAsset = Clothing[ClothIndex];
	if (!ClothAsset) { OutError = TEXT("Clothing asset is null"); return; }

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("mesh_path"), MeshPath);
	Result->SetNumberField(TEXT("cloth_index"), ClothIndex);
	Result->SetStringField(TEXT("cloth_name"), ClothAsset->GetName());

	TArray<TSharedPtr<FJsonValue>> ConfigsArr;
	TArray<UObject*> SubObjects;
	GetObjectsWithOuter(ClothAsset, SubObjects, false);
	for (UObject* Obj : SubObjects)
	{
		if (!IsValid(Obj)) continue;
		FString ClassName = Obj->GetClass()->GetName();
		if (!ClassName.Contains(TEXT("ClothConfig")) && !ClassName.Contains(TEXT("ClothSharedConfig"))) continue;

		TSharedPtr<FJsonObject> ConfigObj = MakeShared<FJsonObject>();
		ConfigObj->SetStringField(TEXT("class"), ClassName);

		for (TFieldIterator<FProperty> PropIt(Obj->GetClass()); PropIt; ++PropIt)
		{
			FProperty* Prop = *PropIt;
			if (!(Prop->GetPropertyFlags() & CPF_Edit)) continue;
			FString PropName = Prop->GetName();
			if (FFloatProperty* FP = CastField<FFloatProperty>(Prop))
				ConfigObj->SetNumberField(PropName, FP->GetPropertyValue_InContainer(Obj));
			else if (FDoubleProperty* DP = CastField<FDoubleProperty>(Prop))
				ConfigObj->SetNumberField(PropName, DP->GetPropertyValue_InContainer(Obj));
			else if (FBoolProperty* BP = CastField<FBoolProperty>(Prop))
				ConfigObj->SetBoolField(PropName, BP->GetPropertyValue_InContainer(Obj));
			else if (FIntProperty* IP = CastField<FIntProperty>(Prop))
				ConfigObj->SetNumberField(PropName, IP->GetPropertyValue_InContainer(Obj));
		}
		ConfigsArr.Add(MakeShared<FJsonValueObject>(ConfigObj));
	}

	if (ConfigsArr.Num() == 0)
		Result->SetStringField(TEXT("note"), TEXT("No ClothConfig sub-objects found. This clothing asset may use a different config format."));

	Result->SetArrayField(TEXT("configs"), ConfigsArr);

	FString OutStr;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutStr);
	FJsonSerializer::Serialize(Result.ToSharedRef(), Writer);
	OutJsonString = OutStr;
}

void HandleSetClothConfigFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	FString MeshPath, ParamName;
	Args->TryGetStringField(TEXT("mesh_path"), MeshPath);
	Args->TryGetStringField(TEXT("param_name"), ParamName);
	if (MeshPath.IsEmpty()) { OutError = TEXT("mesh_path required"); return; }
	if (ParamName.IsEmpty()) { OutError = TEXT("param_name required (e.g. 'GravityScale', 'Damping')"); return; }

	double ClothIdxD = 0, Value = 0;
	Args->TryGetNumberField(TEXT("cloth_index"), ClothIdxD);
	Args->TryGetNumberField(TEXT("value"), Value);
	int32 ClothIndex = (int32)ClothIdxD;

	USkeletalMesh* SkelMesh = LoadObject<USkeletalMesh>(nullptr, *MeshPath);
	if (!SkelMesh) { OutError = FString::Printf(TEXT("SkeletalMesh not found: %s"), *MeshPath); return; }

	const TArray<UClothingAssetBase*>& Clothing = SkelMesh->GetMeshClothingAssets();
	if (ClothIndex < 0 || ClothIndex >= Clothing.Num())
	{
		OutError = FString::Printf(TEXT("cloth_index %d out of range (count=%d)"), ClothIndex, Clothing.Num());
		return;
	}

	UClothingAssetBase* ClothAsset = Clothing[ClothIndex];
	if (!ClothAsset) { OutError = TEXT("Clothing asset is null"); return; }

	bool bFound = false;
	TArray<UObject*> SubObjects;
	GetObjectsWithOuter(ClothAsset, SubObjects, false);
	for (UObject* Obj : SubObjects)
	{
		if (!IsValid(Obj)) continue;
		FString ClassName = Obj->GetClass()->GetName();
		if (!ClassName.Contains(TEXT("ClothConfig")) && !ClassName.Contains(TEXT("ClothSharedConfig"))) continue;

		FName PropFName(*ParamName);
		if (FFloatProperty* FP = FindFProperty<FFloatProperty>(Obj->GetClass(), PropFName))
		{
			FP->SetPropertyValue_InContainer(Obj, (float)Value);
			Obj->Modify();
			bFound = true; break;
		}
		else if (FDoubleProperty* DP = FindFProperty<FDoubleProperty>(Obj->GetClass(), PropFName))
		{
			DP->SetPropertyValue_InContainer(Obj, Value);
			Obj->Modify();
			bFound = true; break;
		}
		else if (FBoolProperty* BP = FindFProperty<FBoolProperty>(Obj->GetClass(), PropFName))
		{
			BP->SetPropertyValue_InContainer(Obj, Value != 0.0);
			Obj->Modify();
			bFound = true; break;
		}
	}

	if (!bFound)
	{
		OutError = FString::Printf(
			TEXT("Property '%s' not found on any ClothConfig for clothing asset index %d. Use get_cloth_config to see available params."),
			*ParamName, ClothIndex);
		return;
	}

	SkelMesh->MarkPackageDirty();
	UEditorAssetLibrary::SaveAsset(MeshPath, false);

	OutJsonString = FString::Printf(
		TEXT("{\"success\":true,\"cloth_index\":%d,\"param_name\":\"%s\",\"value\":%f}"),
		ClothIndex, *ParamName, Value);
}

void HandleAutoGenerateLODs(const FString& MeshPath, int32 LODCount,
	const TArray<float>& ReductionPercents, const TArray<float>& ScreenSizes,
	FString& OutJsonString, FString& OutError)
{
	if (MeshPath.IsEmpty()) { OutError = TEXT("mesh_path is required"); return; }
	if (LODCount < 2 || LODCount > 8) { OutError = TEXT("lod_count must be 2-8"); return; }

	UStaticMesh* Mesh = Cast<UStaticMesh>(UEditorAssetLibrary::LoadAsset(MeshPath));
	if (!Mesh) { OutError = FString::Printf(TEXT("StaticMesh not found: %s"), *MeshPath); return; }

	if (Mesh->GetNumSourceModels() == 0)
	{
		OutError = TEXT("Mesh has no source models — cannot generate LODs");
		return;
	}

	const int32 CurrentCount = Mesh->GetNumSourceModels();
	while (Mesh->GetNumSourceModels() < LODCount)
		Mesh->AddSourceModel();
	while (Mesh->GetNumSourceModels() > LODCount)
		Mesh->RemoveSourceModel(Mesh->GetNumSourceModels() - 1);

	for (int32 i = 1; i < LODCount; i++)
	{
		FStaticMeshSourceModel& SM = Mesh->GetSourceModel(i);
		const float DefaultReductionPct = FMath::Pow(0.5f, (float)i);
		float Pct = ReductionPercents.IsValidIndex(i - 1) ? ReductionPercents[i - 1] : DefaultReductionPct;
		Pct = FMath::Clamp(Pct, 0.01f, 1.0f);
		SM.ReductionSettings.PercentTriangles = Pct;

		if (ScreenSizes.IsValidIndex(i))
		{
			SM.ScreenSize = FPerPlatformFloat(FMath::Clamp(ScreenSizes[i], 0.0f, 1.0f));
		}
		else
		{
			SM.ScreenSize = FPerPlatformFloat(FMath::Pow(0.5f, (float)i));
		}
	}

	if (ScreenSizes.IsValidIndex(0))
		Mesh->GetSourceModel(0).ScreenSize = FPerPlatformFloat(FMath::Clamp(ScreenSizes[0], 0.0f, 1.0f));

#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 7)
	Mesh->SetAutoComputeLODScreenSize(ScreenSizes.Num() == 0);
#else
	Mesh->bAutoComputeLODScreenSize = ScreenSizes.Num() == 0;
#endif
	Mesh->PostEditChange();
	Mesh->MarkPackageDirty();
	UEditorAssetLibrary::SaveAsset(MeshPath, false);

	auto J = SuccessJson();
	J->SetStringField(TEXT("mesh_path"), MeshPath);
	J->SetNumberField(TEXT("lod_count"), LODCount);
	OutJsonString = SerializeJson(J);
	UE_LOG(LogMCPTool, Log, TEXT("auto_generate_lods: %s — %d LODs"), *MeshPath, LODCount);
}

void HandleSetLODScreenSize(const FString& MeshPath, int32 LODIndex, float ScreenSize,
	FString& OutJsonString, FString& OutError)
{
	if (MeshPath.IsEmpty()) { OutError = TEXT("mesh_path is required"); return; }
	UStaticMesh* Mesh = Cast<UStaticMesh>(UEditorAssetLibrary::LoadAsset(MeshPath));
	if (!Mesh) { OutError = FString::Printf(TEXT("StaticMesh not found: %s"), *MeshPath); return; }

	if (LODIndex < 0 || LODIndex >= Mesh->GetNumSourceModels())
	{
		OutError = FString::Printf(TEXT("lod_index %d out of range (mesh has %d LODs)"), LODIndex, Mesh->GetNumSourceModels());
		return;
	}

#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 7)
	Mesh->SetAutoComputeLODScreenSize(false);
#else
	Mesh->bAutoComputeLODScreenSize = false;
#endif
	Mesh->GetSourceModel(LODIndex).ScreenSize = FPerPlatformFloat(FMath::Clamp(ScreenSize, 0.0f, 1.0f));
	Mesh->PostEditChange();
	Mesh->MarkPackageDirty();
	UEditorAssetLibrary::SaveAsset(MeshPath, false);

	auto J = SuccessJson();
	J->SetStringField(TEXT("mesh_path"), MeshPath);
	J->SetNumberField(TEXT("lod_index"), LODIndex);
	J->SetNumberField(TEXT("screen_size"), ScreenSize);
	OutJsonString = SerializeJson(J);
}

void HandleSetTextureMaxSize(const FString& TexturePath, int32 MaxTextureSize,
	FString& OutJsonString, FString& OutError)
{
	if (TexturePath.IsEmpty()) { OutError = TEXT("texture_path is required"); return; }
	UTexture2D* Tex = Cast<UTexture2D>(UEditorAssetLibrary::LoadAsset(TexturePath));
	if (!Tex) { OutError = FString::Printf(TEXT("Texture2D not found: %s"), *TexturePath); return; }

	Tex->MaxTextureSize = MaxTextureSize;
	Tex->PostEditChange();
	Tex->MarkPackageDirty();
	UEditorAssetLibrary::SaveAsset(TexturePath, false);

	auto J = SuccessJson();
	J->SetStringField(TEXT("texture_path"), TexturePath);
	J->SetNumberField(TEXT("max_texture_size"), MaxTextureSize);
	OutJsonString = SerializeJson(J);
}

void HandleCreatePhysicalMaterialFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString Name, SavePath, SurfaceType;
	double Friction = 0.7, Restitution = 0.3, Density = 1.0;
	if (!Args->TryGetStringField(TEXT("name"), Name) || Name.IsEmpty())
	{
		OutError = TEXT("Missing required parameter: name");
		return;
	}
	Args->TryGetStringField(TEXT("save_path"), SavePath);
	Args->TryGetStringField(TEXT("surface_type"), SurfaceType);
	Args->TryGetNumberField(TEXT("friction"), Friction);
	Args->TryGetNumberField(TEXT("restitution"), Restitution);
	Args->TryGetNumberField(TEXT("density"), Density);
	HandleCreatePhysicalMaterial(Name, SavePath, (float)Friction, (float)Restitution, (float)Density,
		SurfaceType, OutJsonString, OutError);
}

void HandleSetPhysicalMaterialPropertiesFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString PhysMatPath, SurfaceType;
	double Friction = -1.0, Restitution = -1.0, Density = -1.0;
	if (!Args->TryGetStringField(TEXT("phys_mat_path"), PhysMatPath) || PhysMatPath.IsEmpty())
	{
		OutError = TEXT("Missing required parameter: phys_mat_path");
		return;
	}
	Args->TryGetStringField(TEXT("surface_type"), SurfaceType);
	Args->TryGetNumberField(TEXT("friction"), Friction);
	Args->TryGetNumberField(TEXT("restitution"), Restitution);
	Args->TryGetNumberField(TEXT("density"), Density);
	HandleSetPhysicalMaterialProperties(PhysMatPath, (float)Friction, (float)Restitution,
		(float)Density, SurfaceType, OutJsonString, OutError);
}

void HandleSetStaticMeshPropertiesFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString MeshPath, EnableNanite, CollisionComplexity;
	double LodBias = -999.0;
	if (!Args->TryGetStringField(TEXT("mesh_path"), MeshPath) || MeshPath.IsEmpty())
	{
		OutError = TEXT("Missing required parameter: mesh_path");
		return;
	}
	Args->TryGetStringField(TEXT("enable_nanite"), EnableNanite);
	Args->TryGetStringField(TEXT("collision_complexity"), CollisionComplexity);
	Args->TryGetNumberField(TEXT("lod_bias"), LodBias);
	HandleSetStaticMeshProperties(MeshPath, EnableNanite, CollisionComplexity, (int32)LodBias,
		OutJsonString, OutError);
}

void HandleSetTexturePropertiesFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString TexturePath, SRGB, CompressionSettings, MipGenSettings;
	if (!Args->TryGetStringField(TEXT("texture_path"), TexturePath) || TexturePath.IsEmpty())
	{
		OutError = TEXT("Missing required parameter: texture_path");
		return;
	}
	Args->TryGetStringField(TEXT("srgb"), SRGB);
	Args->TryGetStringField(TEXT("compression_settings"), CompressionSettings);
	Args->TryGetStringField(TEXT("mip_gen_settings"), MipGenSettings);
	HandleSetTextureProperties(TexturePath, SRGB, CompressionSettings, MipGenSettings,
		OutJsonString, OutError);
}

void HandleAssignPhysicalMaterialFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString TargetPath, ComponentName, PhysMatPath;
	if (!Args->TryGetStringField(TEXT("target_path"), TargetPath) || TargetPath.IsEmpty())
	{
		OutError = TEXT("Missing required parameter: target_path");
		return;
	}
	if (!Args->TryGetStringField(TEXT("component_name"), ComponentName) || ComponentName.IsEmpty())
	{
		OutError = TEXT("Missing required parameter: component_name");
		return;
	}
	if (!Args->TryGetStringField(TEXT("phys_mat_path"), PhysMatPath) || PhysMatPath.IsEmpty())
	{
		OutError = TEXT("Missing required parameter: phys_mat_path");
		return;
	}
	HandleAssignPhysicalMaterial(TargetPath, ComponentName, PhysMatPath, OutJsonString, OutError);
}

void HandleCreateCurveTableFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString Name, SavePath;
	if (!Args->TryGetStringField(TEXT("name"), Name) || Name.IsEmpty())
	{
		OutError = TEXT("Missing required parameter: name");
		return;
	}
	Args->TryGetStringField(TEXT("save_path"), SavePath);
	HandleCreateCurveTable(Name, SavePath, OutJsonString, OutError);
}

void HandleAddCurveTableRowFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString TablePath, RowName;
	if (!Args->TryGetStringField(TEXT("table_path"), TablePath) || TablePath.IsEmpty())
	{
		OutError = TEXT("Missing required parameter: table_path");
		return;
	}
	if (!Args->TryGetStringField(TEXT("row_name"), RowName) || RowName.IsEmpty())
	{
		OutError = TEXT("Missing required parameter: row_name");
		return;
	}
	TArray<float> Times, Values;
	const TArray<TSharedPtr<FJsonValue>>* TimesArr = nullptr;
	const TArray<TSharedPtr<FJsonValue>>* ValuesArr = nullptr;
	if (Args->TryGetArrayField(TEXT("times"), TimesArr) && TimesArr)
		for (const auto& V : *TimesArr) { double D = 0; V->TryGetNumber(D); Times.Add((float)D); }
	if (Args->TryGetArrayField(TEXT("values"), ValuesArr) && ValuesArr)
		for (const auto& V : *ValuesArr) { double D = 0; V->TryGetNumber(D); Values.Add((float)D); }
	HandleAddCurveTableRow(TablePath, RowName, Times, Values, OutJsonString, OutError);
}

void HandleListMeshSocketsFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString MeshPath;
	if (!Args->TryGetStringField(TEXT("mesh_path"), MeshPath) || MeshPath.IsEmpty())
	{
		OutError = TEXT("Missing required parameter: mesh_path");
		return;
	}
	HandleListMeshSockets(MeshPath, OutJsonString, OutError);
}

void HandleListSkeletonBonesFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString MeshPath;
	if (!Args->TryGetStringField(TEXT("mesh_path"), MeshPath) || MeshPath.IsEmpty())
	{
		OutError = TEXT("Missing required parameter: mesh_path");
		return;
	}
	HandleListSkeletonBones(MeshPath, OutJsonString, OutError);
}

void HandleGetStaticMeshInfoFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString MeshPath;
	Args->TryGetStringField(TEXT("mesh_path"), MeshPath);
	HandleGetStaticMeshInfo(MeshPath, OutJsonString, OutError);
}

void HandleGetTextureInfoFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString TexturePath;
	Args->TryGetStringField(TEXT("texture_path"), TexturePath);
	HandleGetTextureInfo(TexturePath, OutJsonString, OutError);
}

void HandleListMorphTargetsFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString MeshPath;
	Args->TryGetStringField(TEXT("mesh_path"), MeshPath);
	HandleListMorphTargets(MeshPath, OutJsonString, OutError);
}

void HandleGetSkeletalMeshInfoFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString MeshPath;
	Args->TryGetStringField(TEXT("mesh_path"), MeshPath);
	HandleGetSkeletalMeshInfo(MeshPath, OutJsonString, OutError);
}

void HandleAssignPhysicsAssetToSkeletalMeshFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString MeshPath, PhysicsAssetPath;
	Args->TryGetStringField(TEXT("mesh_path"), MeshPath);
	Args->TryGetStringField(TEXT("physics_asset_path"), PhysicsAssetPath);
	HandleAssignPhysicsAssetToSkeletalMesh(MeshPath, PhysicsAssetPath, OutJsonString, OutError);
}

void HandleListClothingAssetsFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString MeshPath;
	Args->TryGetStringField(TEXT("mesh_path"), MeshPath);
	HandleListClothingAssets(MeshPath, OutJsonString, OutError);
}

void HandleSplitSkeletalMeshFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString MeshPath, OutputPath;
	double WeightThreshold = 0.5;
	Args->TryGetStringField(TEXT("mesh_path"), MeshPath);
	Args->TryGetStringField(TEXT("output_path"), OutputPath);
	Args->TryGetNumberField(TEXT("weight_threshold"), WeightThreshold);
	TArray<FString> BoneNames, PartitionNames;
	const TArray<TSharedPtr<FJsonValue>>* BA = nullptr;
	if (Args->TryGetArrayField(TEXT("bone_names"), BA))
		for (const auto& V : *BA) { FString S; if (V->TryGetString(S)) BoneNames.Add(S); }
	const TArray<TSharedPtr<FJsonValue>>* PA = nullptr;
	if (Args->TryGetArrayField(TEXT("partition_names"), PA))
		for (const auto& V : *PA) { FString S; if (V->TryGetString(S)) PartitionNames.Add(S); }
	HandleSplitSkeletalMesh(MeshPath, BoneNames, OutputPath, (float)WeightThreshold,
		PartitionNames, OutJsonString, OutError);
}

void HandlePreviewSplitSkeletalMeshFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString MeshPath;
	double WeightThreshold = 0.5;
	Args->TryGetStringField(TEXT("mesh_path"), MeshPath);
	Args->TryGetNumberField(TEXT("weight_threshold"), WeightThreshold);
	TArray<FString> BoneNames;
	const TArray<TSharedPtr<FJsonValue>>* BA = nullptr;
	if (Args->TryGetArrayField(TEXT("bone_names"), BA))
		for (const auto& V : *BA) { FString S; if (V->TryGetString(S)) BoneNames.Add(S); }
	HandlePreviewSplitSkeletalMesh(MeshPath, BoneNames, (float)WeightThreshold, OutJsonString, OutError);
}

void HandleAutoGenerateLODsFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString Path;
	double LC = 4;
	Args->TryGetStringField(TEXT("mesh_path"), Path);
	Args->TryGetNumberField(TEXT("lod_count"), LC);
	TArray<float> ReductionPercents, ScreenSizes;
	const TArray<TSharedPtr<FJsonValue>>* RA = nullptr;
	if (Args->TryGetArrayField(TEXT("reduction_percents"), RA) && RA)
		for (const auto& V : *RA) { double D = 0.5; V->TryGetNumber(D); ReductionPercents.Add((float)D); }
	const TArray<TSharedPtr<FJsonValue>>* SA = nullptr;
	if (Args->TryGetArrayField(TEXT("screen_sizes"), SA) && SA)
		for (const auto& V : *SA) { double D = 0.5; V->TryGetNumber(D); ScreenSizes.Add((float)D); }
	HandleAutoGenerateLODs(Path, (int32)LC, ReductionPercents, ScreenSizes, OutJsonString, OutError);
}

void HandleSetLODScreenSizeFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString Path;
	double Idx = 0, SS = 0.5;
	Args->TryGetStringField(TEXT("mesh_path"), Path);
	Args->TryGetNumberField(TEXT("lod_index"), Idx);
	Args->TryGetNumberField(TEXT("screen_size"), SS);
	HandleSetLODScreenSize(Path, (int32)Idx, (float)SS, OutJsonString, OutError);
}

void HandleSetTextureMaxSizeFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString Path;
	double MaxSize = 0;
	Args->TryGetStringField(TEXT("texture_path"), Path);
	if (Path.IsEmpty()) Args->TryGetStringField(TEXT("asset_path"), Path);
	Args->TryGetNumberField(TEXT("max_texture_size"), MaxSize);
	HandleSetTextureMaxSize(Path, (int32)MaxSize, OutJsonString, OutError);
}

}
