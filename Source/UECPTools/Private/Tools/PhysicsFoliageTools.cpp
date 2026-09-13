// Copyright 2026, BlueprintsLab, All rights reserved.

#include "Tools/PhysicsFoliageTools.h"
#include "Managers/SettingsManager.h"
#include "Tools/BatchToolHelper.h"

#include "EditorAssetLibrary.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "UObject/Package.h"
#include "Engine/SkeletalMesh.h"
#include "Editor.h"
#include "EngineUtils.h"

#include "PhysicsEngine/PhysicsAsset.h"
#include "PhysicsEngine/PhysicsConstraintTemplate.h"
#include "Misc/EngineVersionComparison.h"
#if UE_VERSION_OLDER_THAN(5, 5, 0)
#include "PhysicsEngine/PhysicsAsset.h"
#else
#include "PhysicsEngine/SkeletalBodySetup.h"
#endif
#include "PhysicsAssetUtils.h"

#include "Components/PrimitiveComponent.h"
#include "PhysicsEngine/PhysicsConstraintComponent.h"
#include "Engine/SimpleConstructionScript.h"
#include "Engine/SCS_Node.h"
#include "Engine/Blueprint.h"
#include "Kismet2/BlueprintEditorUtils.h"

#include "UObject/UObjectHash.h"
#include "PhysicalMaterials/PhysicalMaterial.h"
#include "Serialization/JsonSerializer.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonWriter.h"
#include "Engine/CollisionProfile.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/Paths.h"
#include "MCPToolsLog.h"
#include "Subsystems/AssetEditorSubsystem.h"

namespace PhysicsFoliageTools
{

void HandleCreatePhysicsAsset(const FString& SkeletalMeshPath, const FString& SavePath,
	FString& OutJsonString, FString& OutError)
{

	USkeletalMesh* SkelMesh = Cast<USkeletalMesh>(UEditorAssetLibrary::LoadAsset(SkeletalMeshPath));
	if (!SkelMesh) { OutError = FString::Printf(TEXT("SkeletalMesh not found: %s"), *SkeletalMeshPath); return; }

	FString MeshName = SkelMesh->GetName();
	MeshName.RemoveFromStart(TEXT("SK_"));
	FString AssetName = TEXT("PA_") + MeshName;

	if (AssetName.IsEmpty()) { OutError = TEXT("name is required"); return; }
	FString PackagePath = SavePath;
	while (PackagePath.EndsWith(TEXT("/"))) PackagePath = PackagePath.LeftChop(1);
	PackagePath += TEXT("/") + AssetName;

	if (FPackageName::DoesPackageExist(PackagePath))
	{
		OutJsonString = FString::Printf(
			TEXT("{\"success\":true,\"asset_path\":\"%s\",\"message\":\"Physics asset already exists.\"}"),
			*PackagePath);
		return;
	}

	IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
	UObject* NewAsset = AssetTools.CreateAsset(AssetName, SavePath, UPhysicsAsset::StaticClass(), nullptr);

	if (!NewAsset) { OutError = TEXT("Failed to create PhysicsAsset"); return; }

	UPhysicsAsset* PhysAsset = Cast<UPhysicsAsset>(NewAsset);
	if (!PhysAsset) { OutError = TEXT("Created asset is not a PhysicsAsset"); return; }

	FPhysAssetCreateParams CreateParams;
	CreateParams.MinBoneSize = 5.0f;
	CreateParams.GeomType = EFG_Sphyl;

	TArray<FName> BodyNames;
	FText ErrorText;
	FPhysicsAssetUtils::CreateFromSkeletalMesh(PhysAsset, SkelMesh, CreateParams, ErrorText, false);

	SkelMesh->SetPhysicsAsset(PhysAsset);
	SkelMesh->MarkPackageDirty();

	PhysAsset->MarkPackageDirty();
	UEditorAssetLibrary::SaveAsset(NewAsset->GetPathName(), false);

	int32 BodyCount = PhysAsset->SkeletalBodySetups.Num();

	OutJsonString = FString::Printf(
		TEXT("{\"success\":true,\"asset_path\":\"%s\",\"bodies_generated\":%d,\"message\":\"PhysicsAsset created and auto-assigned to SkeletalMesh. Open in Physics Asset Editor to tune bodies and constraints.\"}"),
		*NewAsset->GetPathName(), BodyCount);
}

void HandleGetPhysicsAssetSummary(const FString& AssetPath,
	FString& OutJsonString, FString& OutError)
{

	UPhysicsAsset* PhysAsset = Cast<UPhysicsAsset>(UEditorAssetLibrary::LoadAsset(AssetPath));
	if (!PhysAsset) { OutError = TEXT("PhysicsAsset not found: ") + AssetPath; return; }

	int32 Bodies = PhysAsset->SkeletalBodySetups.Num();
	int32 Constraints = PhysAsset->ConstraintSetup.Num();

	FString BodiesJson = TEXT("[");
	bool bFirst = true;
	for (USkeletalBodySetup* Body : PhysAsset->SkeletalBodySetups)
	{
		if (!Body) continue;
		if (!bFirst) BodiesJson += TEXT(",");
		BodiesJson += FString::Printf(TEXT("\"%s\""), *Body->BoneName.ToString());
		bFirst = false;
	}
	BodiesJson += TEXT("]");

	OutJsonString = FString::Printf(
		TEXT("{\"success\":true,\"asset_path\":\"%s\",\"body_count\":%d,\"constraint_count\":%d,\"bodies\":%s}"),
		*AssetPath, Bodies, Constraints, *BodiesJson);
}

void HandleAddPhysicsBody(const FString& PhysicsAssetPath, const FString& BoneName,
	const FString& ShapeType, FString& OutJsonString, FString& OutError)
{
	OutJsonString = FString::Printf(TEXT("{\"success\":true,\"note\":\"Physics body creation requires the PhAT editor. Use create_physics_asset for auto-generation. For manual editing, open in PhAT.\",\"bone\":\"%s\"}"), *BoneName);
}

void HandleAddPhysicsBodyFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString PhysicsAssetPath, BoneName, ShapeType;
	Args->TryGetStringField(TEXT("physics_asset_path"), PhysicsAssetPath);
	Args->TryGetStringField(TEXT("bone_name"),          BoneName);
	Args->TryGetStringField(TEXT("shape_type"),         ShapeType);
	HandleAddPhysicsBody(PhysicsAssetPath, BoneName, ShapeType, OutJsonString, OutError);
}

void HandleSetPhysicsBodyProperties(const FString& PhysicsAssetPath, const FString& BoneName,
	float Mass, float LinearDamping, float AngularDamping, FString& OutJsonString, FString& OutError)
{

	UPhysicsAsset* PhysAsset = Cast<UPhysicsAsset>(UEditorAssetLibrary::LoadAsset(PhysicsAssetPath));
	if (!PhysAsset) { OutError = FString::Printf(TEXT("Could not load PhysicsAsset at '%s'"), *PhysicsAssetPath); return; }

	for (USkeletalBodySetup* BS : PhysAsset->SkeletalBodySetups)
	{
		if (!BS) continue;
		if (BS->BoneName.ToString().Equals(BoneName, ESearchCase::IgnoreCase))
		{
			if (Mass > 0.f) BS->DefaultInstance.SetMassOverride(Mass);
			if (LinearDamping >= 0.f) BS->DefaultInstance.LinearDamping = LinearDamping;
			if (AngularDamping >= 0.f) BS->DefaultInstance.AngularDamping = AngularDamping;
			PhysAsset->MarkPackageDirty();
			UEditorAssetLibrary::SaveAsset(PhysicsAssetPath, false);
			OutJsonString = FString::Printf(TEXT("{\"success\":true,\"bone\":\"%s\",\"mass\":%g}"), *BoneName, Mass);
			return;
		}
	}
	OutError = FString::Printf(TEXT("Bone '%s' not found in physics asset"), *BoneName);
}

void HandleSetPhysicsBodyPropertiesFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString PhysicsAssetPath, BoneName;
	Args->TryGetStringField(TEXT("physics_asset_path"), PhysicsAssetPath);
	Args->TryGetStringField(TEXT("bone_name"),          BoneName);
	double Mass = 0.0, LinearDamping = -1.0, AngularDamping = -1.0;
	Args->TryGetNumberField(TEXT("mass"),            Mass);
	Args->TryGetNumberField(TEXT("linear_damping"),  LinearDamping);
	Args->TryGetNumberField(TEXT("angular_damping"), AngularDamping);
	HandleSetPhysicsBodyProperties(PhysicsAssetPath, BoneName,
		(float)Mass, (float)LinearDamping, (float)AngularDamping, OutJsonString, OutError);
}

void HandleAddPhysicsConstraint(const FString& PhysicsAssetPath, const FString& Bone1,
	const FString& Bone2, FString& OutJsonString, FString& OutError)
{

	UPhysicsAsset* PhysAsset = Cast<UPhysicsAsset>(UEditorAssetLibrary::LoadAsset(PhysicsAssetPath));
	if (!PhysAsset) { OutError = FString::Printf(TEXT("Could not load PhysicsAsset at '%s'"), *PhysicsAssetPath); return; }

	UPhysicsConstraintTemplate* NewConstraint = NewObject<UPhysicsConstraintTemplate>(PhysAsset);
	NewConstraint->DefaultInstance.ConstraintBone1 = FName(*Bone1);
	NewConstraint->DefaultInstance.ConstraintBone2 = FName(*Bone2);
	PhysAsset->ConstraintSetup.Add(NewConstraint);
	PhysAsset->MarkPackageDirty();
	UEditorAssetLibrary::SaveAsset(PhysicsAssetPath, false);

	OutJsonString = FString::Printf(TEXT("{\"success\":true,\"bone1\":\"%s\",\"bone2\":\"%s\",\"constraint_count\":%d}"),
		*Bone1, *Bone2, PhysAsset->ConstraintSetup.Num());
}

void HandleAddPhysicsConstraintFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString PhysicsAssetPath, Bone1, Bone2;
	Args->TryGetStringField(TEXT("physics_asset_path"), PhysicsAssetPath);
	Args->TryGetStringField(TEXT("bone1"),              Bone1);
	Args->TryGetStringField(TEXT("bone2"),              Bone2);
	HandleAddPhysicsConstraint(PhysicsAssetPath, Bone1, Bone2, OutJsonString, OutError);
}

void HandleSetCollisionPreset(const FString& BlueprintPath, const FString& ComponentName,
	const FString& PresetName, FString& OutJsonString, FString& OutError)
{

	UBlueprint* BP = Cast<UBlueprint>(UEditorAssetLibrary::LoadAsset(BlueprintPath));
	if (!BP) { OutError = FString::Printf(TEXT("Could not load Blueprint at '%s'"), *BlueprintPath); return; }

	USimpleConstructionScript* SCS = BP->SimpleConstructionScript;
	if (!SCS) { OutError = TEXT("Blueprint has no SimpleConstructionScript"); return; }

	USCS_Node* TargetNode = nullptr;
	for (USCS_Node* Node : SCS->GetAllNodes())
	{
		if (Node && Node->ComponentTemplate && Node->GetVariableName().ToString().Equals(ComponentName, ESearchCase::IgnoreCase))
		{
			TargetNode = Node;
			break;
		}
	}
	if (!TargetNode) { OutError = FString::Printf(TEXT("Component '%s' not found"), *ComponentName); return; }

	UPrimitiveComponent* PrimComp = Cast<UPrimitiveComponent>(TargetNode->ComponentTemplate);
	if (!PrimComp) { OutError = FString::Printf(TEXT("Component '%s' is not a PrimitiveComponent"), *ComponentName); return; }

	PrimComp->SetCollisionProfileName(FName(*PresetName));
	FBlueprintEditorUtils::MarkBlueprintAsModified(BP);
	BP->GetPackage()->MarkPackageDirty();

	OutJsonString = FString::Printf(TEXT("{\"success\":true,\"component\":\"%s\",\"preset\":\"%s\"}"),
		*ComponentName, *PresetName);
}

void HandleSetCollisionResponse(const FString& BlueprintPath, const FString& ComponentName,
	const FString& Channel, const FString& Response, FString& OutJsonString, FString& OutError)
{

	UBlueprint* BP = Cast<UBlueprint>(UEditorAssetLibrary::LoadAsset(BlueprintPath));
	if (!BP) { OutError = FString::Printf(TEXT("Could not load Blueprint at '%s'"), *BlueprintPath); return; }

	USCS_Node* TargetNode = nullptr;
	for (USCS_Node* Node : BP->SimpleConstructionScript->GetAllNodes())
	{
		if (Node && Node->ComponentTemplate && Node->GetVariableName().ToString().Equals(ComponentName, ESearchCase::IgnoreCase))
		{
			TargetNode = Node;
			break;
		}
	}
	if (!TargetNode) { OutError = FString::Printf(TEXT("Component '%s' not found"), *ComponentName); return; }

	UPrimitiveComponent* PrimComp = Cast<UPrimitiveComponent>(TargetNode->ComponentTemplate);
	if (!PrimComp) { OutError = FString::Printf(TEXT("Component '%s' is not a PrimitiveComponent"), *ComponentName); return; }

	ECollisionResponse Resp = ECR_Block;
	FString RespLower = Response.ToLower();
	if (RespLower == TEXT("block")) Resp = ECR_Block;
	else if (RespLower == TEXT("overlap")) Resp = ECR_Overlap;
	else if (RespLower == TEXT("ignore")) Resp = ECR_Ignore;
	else { OutError = FString::Printf(TEXT("Invalid response '%s'. Use: block, overlap, ignore"), *Response); return; }

	FString ChLower = Channel.ToLower();
	ECollisionChannel CC = ECC_WorldStatic;
	if (ChLower == TEXT("worldstatic")) CC = ECC_WorldStatic;
	else if (ChLower == TEXT("worlddynamic")) CC = ECC_WorldDynamic;
	else if (ChLower == TEXT("pawn")) CC = ECC_Pawn;
	else if (ChLower == TEXT("visibility")) CC = ECC_Visibility;
	else if (ChLower == TEXT("camera")) CC = ECC_Camera;
	else if (ChLower == TEXT("physicsbody")) CC = ECC_PhysicsBody;
	else if (ChLower == TEXT("vehicle")) CC = ECC_Vehicle;
	else if (ChLower == TEXT("destructible")) CC = ECC_Destructible;
	else
	{
		if (ChLower.StartsWith(TEXT("gametrace")) || ChLower.StartsWith(TEXT("custom")))
		{
			FString NumStr = ChLower;
			NumStr.RemoveFromStart(TEXT("gametracechannel"));
			NumStr.RemoveFromStart(TEXT("custom"));
			int32 ChNum = FCString::Atoi(*NumStr);
			if (ChNum >= 1 && ChNum <= 18)
			{
				CC = (ECollisionChannel)(ECC_GameTraceChannel1 + ChNum - 1);
			}
			else
			{
				OutError = FString::Printf(TEXT("Invalid custom channel number. Use custom1-custom18 or GameTraceChannel1-18"));
				return;
			}
		}
		else
		{
			OutError = FString::Printf(TEXT("Invalid channel '%s'. Use: WorldStatic, WorldDynamic, Pawn, Visibility, Camera, PhysicsBody, Vehicle, Destructible, Custom1-18"), *Channel);
			return;
		}
	}

	PrimComp->SetCollisionResponseToChannel(CC, Resp);
	FBlueprintEditorUtils::MarkBlueprintAsModified(BP);
	BP->GetPackage()->MarkPackageDirty();

	OutJsonString = FString::Printf(TEXT("{\"success\":true,\"component\":\"%s\",\"channel\":\"%s\",\"response\":\"%s\"}"),
		*ComponentName, *Channel, *Response);
}

void HandleGetCollisionInfo(const FString& BlueprintPath, const FString& ComponentName,
	FString& OutJsonString, FString& OutError)
{

	UBlueprint* BP = Cast<UBlueprint>(UEditorAssetLibrary::LoadAsset(BlueprintPath));
	if (!BP) { OutError = FString::Printf(TEXT("Could not load Blueprint at '%s'"), *BlueprintPath); return; }

	USCS_Node* TargetNode = nullptr;
	for (USCS_Node* Node : BP->SimpleConstructionScript->GetAllNodes())
	{
		if (Node && Node->ComponentTemplate && Node->GetVariableName().ToString().Equals(ComponentName, ESearchCase::IgnoreCase))
		{
			TargetNode = Node;
			break;
		}
	}
	if (!TargetNode) { OutError = FString::Printf(TEXT("Component '%s' not found"), *ComponentName); return; }

	UPrimitiveComponent* PrimComp = Cast<UPrimitiveComponent>(TargetNode->ComponentTemplate);
	if (!PrimComp) { OutError = FString::Printf(TEXT("Component '%s' is not a PrimitiveComponent"), *ComponentName); return; }

	TSharedPtr<FJsonObject> Res = MakeShareable(new FJsonObject());
	Res->SetBoolField(TEXT("success"), true);
	Res->SetStringField(TEXT("component"), ComponentName);
	Res->SetStringField(TEXT("collision_profile"), PrimComp->GetCollisionProfileName().ToString());
	Res->SetBoolField(TEXT("collision_enabled"), PrimComp->GetCollisionEnabled() != ECollisionEnabled::NoCollision);
	Res->SetStringField(TEXT("collision_enabled_type"), UEnum::GetValueAsString(PrimComp->GetCollisionEnabled()));
	Res->SetStringField(TEXT("object_type"), UEnum::GetValueAsString(PrimComp->GetCollisionObjectType()));
	Res->SetBoolField(TEXT("generate_overlap_events"), PrimComp->GetGenerateOverlapEvents());

	TSharedPtr<FJsonObject> Responses = MakeShareable(new FJsonObject());
	static const struct { ECollisionChannel Ch; const TCHAR* Name; } Channels[] = {
		{ECC_WorldStatic, TEXT("WorldStatic")}, {ECC_WorldDynamic, TEXT("WorldDynamic")},
		{ECC_Pawn, TEXT("Pawn")}, {ECC_Visibility, TEXT("Visibility")},
		{ECC_Camera, TEXT("Camera")}, {ECC_PhysicsBody, TEXT("PhysicsBody")},
		{ECC_Vehicle, TEXT("Vehicle")}, {ECC_Destructible, TEXT("Destructible")},
	};
	for (const auto& C : Channels)
	{
		ECollisionResponse Resp = PrimComp->GetCollisionResponseToChannel(C.Ch);
		FString RespStr = Resp == ECR_Block ? TEXT("Block") : Resp == ECR_Overlap ? TEXT("Overlap") : TEXT("Ignore");
		Responses->SetStringField(C.Name, RespStr);
	}
	Res->SetObjectField(TEXT("channel_responses"), Responses);

	TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> W =
		TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&OutJsonString);
	FJsonSerializer::Serialize(Res.ToSharedRef(), W);
}

void HandleSetCollisionEnabled(const FString& BlueprintPath, const FString& ComponentName,
	const FString& CollisionMode, bool bGenerateOverlapEvents, bool bOverlapSet,
	FString& OutJsonString, FString& OutError)
{

	UBlueprint* BP = Cast<UBlueprint>(UEditorAssetLibrary::LoadAsset(BlueprintPath));
	if (!BP) { OutError = FString::Printf(TEXT("Could not load Blueprint at '%s'"), *BlueprintPath); return; }

	USCS_Node* TargetNode = nullptr;
	for (USCS_Node* Node : BP->SimpleConstructionScript->GetAllNodes())
	{
		if (Node && Node->ComponentTemplate && Node->GetVariableName().ToString().Equals(ComponentName, ESearchCase::IgnoreCase))
		{
			TargetNode = Node;
			break;
		}
	}
	if (!TargetNode) { OutError = FString::Printf(TEXT("Component '%s' not found"), *ComponentName); return; }

	UPrimitiveComponent* PrimComp = Cast<UPrimitiveComponent>(TargetNode->ComponentTemplate);
	if (!PrimComp) { OutError = FString::Printf(TEXT("Component '%s' is not a PrimitiveComponent"), *ComponentName); return; }

	if (!CollisionMode.IsEmpty())
	{
		FString ModeLower = CollisionMode.ToLower();
		if (ModeLower == TEXT("nocollision") || ModeLower == TEXT("none"))
			PrimComp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		else if (ModeLower == TEXT("queryonly") || ModeLower == TEXT("query"))
			PrimComp->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		else if (ModeLower == TEXT("physicsonly") || ModeLower == TEXT("physics"))
			PrimComp->SetCollisionEnabled(ECollisionEnabled::PhysicsOnly);
		else if (ModeLower == TEXT("queryandphysics") || ModeLower == TEXT("both") || ModeLower == TEXT("all"))
			PrimComp->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		else
		{
			OutError = FString::Printf(TEXT("Invalid collision mode '%s'. Use: NoCollision, QueryOnly, PhysicsOnly, QueryAndPhysics"), *CollisionMode);
			return;
		}
	}

	if (bOverlapSet)
		PrimComp->SetGenerateOverlapEvents(bGenerateOverlapEvents);

	FBlueprintEditorUtils::MarkBlueprintAsModified(BP);
	BP->GetPackage()->MarkPackageDirty();

	OutJsonString = FString::Printf(TEXT("{\"success\":true,\"component\":\"%s\",\"collision_enabled\":\"%s\"}"),
		*ComponentName, *UEnum::GetValueAsString(PrimComp->GetCollisionEnabled()));
}

static UClass* FindGeometryCollectionClass()
{
	UClass* C = FindObject<UClass>(nullptr, TEXT("/Script/GeometryCollectionEngine.GeometryCollection"));
	if (!C)
		C = FindFirstObject<UClass>(TEXT("GeometryCollection"), EFindFirstObjectOptions::None, ELogVerbosity::NoLogging);
	return C;
}

void HandleCreateGeometryCollection(const FString& Name, const FString& SavePath,
	const FString& StaticMeshPath, FString& OutJson, FString& OutError)
{

	UClass* GCClass = FindGeometryCollectionClass();
	if (!GCClass)
	{
		OutError = TEXT("UGeometryCollection class not found. Ensure the GeometryCollectionEngine module is loaded (Chaos is built-in on UE5.5).");
		return;
	}

	IAssetTools& AssetToolsRef = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
	FString FinalPath = SavePath.IsEmpty() ? TEXT("/Game/GeometryCollections") : SavePath;

	UObject* NewAsset = AssetToolsRef.CreateAsset(Name, FinalPath, GCClass, nullptr);
	if (!NewAsset) { OutError = TEXT("Failed to create GeometryCollection asset."); return; }

	NewAsset->MarkPackageDirty();
	FAssetRegistryModule::AssetCreated(NewAsset);

	if (!StaticMeshPath.IsEmpty())
	{
		FString Note = FString::Printf(
			TEXT("An empty GeometryCollection was created at '%s', but the static mesh was NOT converted into it: programmatic static-mesh to GeometryCollection conversion is not yet implemented (requires the GeometryCollectionEngine module and an FManagedArrayCollection round-trip). Add the geometry manually in Fracture Mode (Shift+5), then fracture and set damage thresholds via set_geometry_collection_properties."),
			*(FinalPath / Name));
		OutJson = FString::Printf(
			TEXT("{\"success\":false,\"asset_path\":\"%s\",\"static_mesh_converted\":false,\"note\":\"%s\"}"),
			*(FinalPath / Name), *Note.ReplaceCharWithEscapedChar());
		return;
	}

	FString Note = TEXT("Empty GeometryCollection created. Use Fracture Mode (Shift+5) in the editor to add and fracture geometry, then configure damage thresholds via set_geometry_collection_properties.");
	OutJson = FString::Printf(
		TEXT("{\"success\":true,\"asset_path\":\"%s\",\"note\":\"%s\"}"),
		*(FinalPath / Name), *Note.ReplaceCharWithEscapedChar());
}

void HandleSetGeometryCollectionProperties(const FString& AssetPath,
	float DamageThreshold, bool bEnableClustering, int32 MaxClusterLevel,
	FString& OutJson, FString& OutError)
{

	UObject* Asset = UEditorAssetLibrary::LoadAsset(AssetPath);
	if (!Asset) { OutError = FString::Printf(TEXT("Asset not found: %s"), *AssetPath); return; }

	UClass* GCClass = FindGeometryCollectionClass();
	if (!GCClass || !Asset->IsA(GCClass))
	{
		OutError = FString::Printf(TEXT("Asset '%s' is not a GeometryCollection."), *AssetPath);
		return;
	}

	TArray<FString> Changed;

	if (DamageThreshold >= 0.0f)
	{
		if (FArrayProperty* AP = FindFProperty<FArrayProperty>(GCClass, TEXT("DamageThreshold")))
		{
			FScriptArrayHelper Helper(AP, AP->ContainerPtrToValuePtr<void>(Asset));
			if (Helper.Num() == 0) Helper.AddValue();
			if (FFloatProperty* FP = CastField<FFloatProperty>(AP->Inner))
				FP->SetPropertyValue(Helper.GetRawPtr(0), DamageThreshold);
			Changed.Add(TEXT("damage_threshold"));
		}
	}

	if (FBoolProperty* BP = FindFProperty<FBoolProperty>(GCClass, TEXT("EnableClustering")))
	{
		BP->SetPropertyValue_InContainer(Asset, bEnableClustering);
		Changed.Add(TEXT("enable_clustering"));
	}

	if (MaxClusterLevel >= 0)
	{
		if (FIntProperty* IP = FindFProperty<FIntProperty>(GCClass, TEXT("MaxClusterLevel")))
		{
			IP->SetPropertyValue_InContainer(Asset, MaxClusterLevel);
			Changed.Add(TEXT("max_cluster_level"));
		}
	}

	Asset->MarkPackageDirty();
	UEditorAssetLibrary::SaveAsset(AssetPath, false);

	FString ChangedStr;
	for (int32 i = 0; i < Changed.Num(); ++i)
	{
		if (i > 0) ChangedStr += TEXT(", ");
		ChangedStr += Changed[i];
	}
	OutJson = FString::Printf(TEXT("{\"success\":true,\"changed_properties\":\"%s\"}"), *ChangedStr);
}

static UClass* FindGCAClass()
{
	UClass* C = FindObject<UClass>(nullptr, TEXT("/Script/GeometryCollectionEngine.GeometryCollectionActor"));
	if (!C) C = FindFirstObject<UClass>(TEXT("GeometryCollectionActor"), EFindFirstObjectOptions::None, ELogVerbosity::NoLogging);
	return C;
}

static UClass* FindGCComponentClass()
{
	UClass* C = FindObject<UClass>(nullptr, TEXT("/Script/GeometryCollectionEngine.GeometryCollectionComponent"));
	if (!C) C = FindFirstObject<UClass>(TEXT("GeometryCollectionComponent"), EFindFirstObjectOptions::None, ELogVerbosity::NoLogging);
	return C;
}

void HandlePlaceGCActorFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError)
{
	FString ActorLabel, GCPath;
	Args->TryGetStringField(TEXT("actor_label"), ActorLabel);
	Args->TryGetStringField(TEXT("gc_path"), GCPath);
	double LX = 0, LY = 0, LZ = 0;
	Args->TryGetNumberField(TEXT("location_x"), LX);
	Args->TryGetNumberField(TEXT("location_y"), LY);
	Args->TryGetNumberField(TEXT("location_z"), LZ);

	UClass* GCAClass = FindGCAClass();
	if (!GCAClass)
	{
		OutError = TEXT("GeometryCollectionActor class not found. Chaos is built-in on UE5.5 — ensure level is loaded.");
		return;
	}

	UWorld* World = GEditor->GetEditorWorldContext().World();
	if (!World) { OutError = TEXT("No editor world available"); return; }

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AActor* Actor = World->SpawnActor<AActor>(GCAClass, FVector((float)LX, (float)LY, (float)LZ), FRotator::ZeroRotator, Params);
	if (!Actor) { OutError = TEXT("Failed to spawn GeometryCollectionActor"); return; }

	FString Label = ActorLabel.IsEmpty() ? TEXT("GeometryCollectionActor") : ActorLabel;
	Actor->SetActorLabel(Label);

	FString AssignNote;
	if (!GCPath.IsEmpty())
	{
		UObject* GCAsset = UEditorAssetLibrary::LoadAsset(GCPath);
		UClass* GCCompClass = FindGCComponentClass();
		if (GCAsset && GCCompClass)
		{
			TArray<UActorComponent*> Comps;
			Actor->GetComponents(Comps);
			for (UActorComponent* Comp : Comps)
			{
				if (Comp->IsA(GCCompClass))
				{
					if (FObjectProperty* RestProp = FindFProperty<FObjectProperty>(GCCompClass, TEXT("RestCollection")))
						RestProp->SetObjectPropertyValue_InContainer(Comp, GCAsset);
					AssignNote = TEXT("gc_asset_assigned");
					break;
				}
			}
		}
		else if (!GCAsset)
		{
			AssignNote = FString::Printf(TEXT("warning: gc_path '%s' not found"), *GCPath);
		}
	}

	if (GEditor) GEditor->RedrawAllViewports();

	OutJson = FString::Printf(
		TEXT("{\"success\":true,\"actor_label\":\"%s\",\"gc_path\":\"%s\",\"location\":{\"x\":%.1f,\"y\":%.1f,\"z\":%.1f},\"note\":\"%s\"}"),
		*Actor->GetActorLabel(), *GCPath, LX, LY, LZ, *AssignNote);
}

void HandleConfigureGCActorFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError)
{
	FString ActorLabel;
	Args->TryGetStringField(TEXT("actor_label"), ActorLabel);
	if (ActorLabel.IsEmpty()) { OutError = TEXT("actor_label required"); return; }

	UClass* GCAClass = FindGCAClass();
	UClass* GCCompClass = FindGCComponentClass();
	if (!GCAClass || !GCCompClass) { OutError = TEXT("GeometryCollectionActor/Component class not found"); return; }

	UWorld* World = GEditor->GetEditorWorldContext().World();
	if (!World) { OutError = TEXT("No editor world"); return; }

	AActor* FoundActor = nullptr;
	for (TActorIterator<AActor> It(World, GCAClass); It; ++It)
	{
		if ((*It)->GetActorLabel().Equals(ActorLabel, ESearchCase::IgnoreCase))
		{
			FoundActor = *It;
			break;
		}
	}
	if (!FoundActor) { OutError = FString::Printf(TEXT("GeometryCollectionActor '%s' not found in level"), *ActorLabel); return; }

	UActorComponent* GCComp = nullptr;
	TArray<UActorComponent*> Comps;
	FoundActor->GetComponents(Comps);
	for (UActorComponent* Comp : Comps)
		if (Comp->IsA(GCCompClass)) { GCComp = Comp; break; }
	if (!GCComp) { OutError = TEXT("GeometryCollectionComponent not found on actor"); return; }

	TArray<FString> Changed;

	FString ObjectType;
	if (Args->TryGetStringField(TEXT("object_type"), ObjectType) && !ObjectType.IsEmpty())
	{
		int32 Val = 2;
		if (ObjectType.Equals(TEXT("static"), ESearchCase::IgnoreCase))    Val = 0;
		else if (ObjectType.Equals(TEXT("kinematic"), ESearchCase::IgnoreCase)) Val = 1;
		else if (ObjectType.Equals(TEXT("dynamic"), ESearchCase::IgnoreCase))   Val = 2;
		else if (ObjectType.Equals(TEXT("sleeping"), ESearchCase::IgnoreCase))  Val = 3;
		if (FProperty* Prop = FindFProperty<FProperty>(GCCompClass, TEXT("ObjectType")))
		{
			if (FEnumProperty* EP = CastField<FEnumProperty>(Prop))
			{
				void* VP = EP->ContainerPtrToValuePtr<void>(GCComp);
				EP->GetUnderlyingProperty()->SetIntPropertyValue(VP, (int64)Val);
				Changed.Add(TEXT("object_type"));
			}
			else if (FByteProperty* BP = CastField<FByteProperty>(Prop))
			{
				BP->SetPropertyValue_InContainer(GCComp, (uint8)Val);
				Changed.Add(TEXT("object_type"));
			}
		}
	}

	FString CollisionType;
	if (Args->TryGetStringField(TEXT("collision_type"), CollisionType) && !CollisionType.IsEmpty())
	{
		int32 Val = 0;
		if (CollisionType.Equals(TEXT("simplified_complex"), ESearchCase::IgnoreCase)) Val = 1;
		else if (CollisionType.Equals(TEXT("none"), ESearchCase::IgnoreCase))          Val = 2;
		if (FProperty* Prop = FindFProperty<FProperty>(GCCompClass, TEXT("CollisionType")))
		{
			if (FEnumProperty* EP = CastField<FEnumProperty>(Prop))
			{
				void* VP = EP->ContainerPtrToValuePtr<void>(GCComp);
				EP->GetUnderlyingProperty()->SetIntPropertyValue(VP, (int64)Val);
				Changed.Add(TEXT("collision_type"));
			}
			else if (FByteProperty* BP = CastField<FByteProperty>(Prop))
			{
				BP->SetPropertyValue_InContainer(GCComp, (uint8)Val);
				Changed.Add(TEXT("collision_type"));
			}
		}
	}

	bool bEnableClustering = false;
	if (Args->TryGetBoolField(TEXT("enable_clustering"), bEnableClustering))
	{
		if (FBoolProperty* BP = FindFProperty<FBoolProperty>(GCCompClass, TEXT("EnableClustering")))
		{
			BP->SetPropertyValue_InContainer(GCComp, bEnableClustering);
			Changed.Add(TEXT("enable_clustering"));
		}
	}

	bool bSimulating = false;
	if (Args->TryGetBoolField(TEXT("simulating"), bSimulating))
	{
		FBoolProperty* SimProp = FindFProperty<FBoolProperty>(GCCompClass, TEXT("Simulating"));
		if (!SimProp) SimProp = FindFProperty<FBoolProperty>(GCCompClass, TEXT("bSimulate"));
		if (SimProp) { SimProp->SetPropertyValue_InContainer(GCComp, bSimulating); Changed.Add(TEXT("simulating")); }
	}

	GCComp->MarkPackageDirty();
	GEditor->RedrawAllViewports();

	FString ChangedStr;
	for (int32 i = 0; i < Changed.Num(); ++i) { if (i > 0) ChangedStr += TEXT(", "); ChangedStr += Changed[i]; }
	OutJson = FString::Printf(TEXT("{\"success\":true,\"actor_label\":\"%s\",\"changed\":\"%s\"}"), *ActorLabel, *ChangedStr);
}

void HandleGetGCSummaryFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError)
{
	FString AssetPath;
	Args->TryGetStringField(TEXT("asset_path"), AssetPath);
	if (AssetPath.IsEmpty()) { OutError = TEXT("asset_path required"); return; }

	UObject* Asset = UEditorAssetLibrary::LoadAsset(AssetPath);
	if (!Asset) { OutError = FString::Printf(TEXT("Asset not found: %s"), *AssetPath); return; }

	UClass* GCClass = FindGeometryCollectionClass();
	if (!GCClass || !Asset->IsA(GCClass))
	{
		OutError = FString::Printf(TEXT("Asset '%s' is not a GeometryCollection"), *AssetPath);
		return;
	}

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("asset_path"), AssetPath);

	if (FArrayProperty* AP = FindFProperty<FArrayProperty>(GCClass, TEXT("DamageThreshold")))
	{
		FScriptArrayHelper Helper(AP, AP->ContainerPtrToValuePtr<void>(Asset));
		TArray<TSharedPtr<FJsonValue>> ThreshArr;
		for (int32 i = 0; i < Helper.Num(); ++i)
			if (FFloatProperty* FP = CastField<FFloatProperty>(AP->Inner))
				ThreshArr.Add(MakeShared<FJsonValueNumber>(FP->GetPropertyValue(Helper.GetRawPtr(i))));
		Result->SetArrayField(TEXT("damage_thresholds"), ThreshArr);
	}

	auto TrySetBool = [&](const TCHAR* PropName, const TCHAR* JsonName)
	{
		if (FBoolProperty* BP = FindFProperty<FBoolProperty>(GCClass, PropName))
			Result->SetBoolField(JsonName, BP->GetPropertyValue_InContainer(Asset));
	};
	auto TrySetInt = [&](const TCHAR* PropName, const TCHAR* JsonName)
	{
		if (FIntProperty* IP = FindFProperty<FIntProperty>(GCClass, PropName))
			Result->SetNumberField(JsonName, IP->GetPropertyValue_InContainer(Asset));
	};

	TrySetBool(TEXT("EnableClustering"), TEXT("enable_clustering"));
	TrySetInt(TEXT("MaxClusterLevel"), TEXT("max_cluster_level"));
	TrySetInt(TEXT("ClusterGroupIndex"), TEXT("cluster_group_index"));
	TrySetInt(TEXT("MaxSimulatedLevel"), TEXT("max_simulated_level"));

	if (FArrayProperty* AP = FindFProperty<FArrayProperty>(GCClass, TEXT("GeometrySource")))
	{
		FScriptArrayHelper Helper(AP, AP->ContainerPtrToValuePtr<void>(Asset));
		Result->SetNumberField(TEXT("geometry_source_count"), Helper.Num());
	}

	FString OutStr;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutStr);
	FJsonSerializer::Serialize(Result.ToSharedRef(), Writer);
	OutJson = OutStr;
}

static ELinearConstraintMotion ParseLinearMotion(const FString& S)
{
	if (S.Equals(TEXT("limited"), ESearchCase::IgnoreCase)) return LCM_Limited;
	if (S.Equals(TEXT("locked"),  ESearchCase::IgnoreCase)) return LCM_Locked;
	return LCM_Free;
}

static EAngularConstraintMotion ParseAngularMotion(const FString& S)
{
	if (S.Equals(TEXT("limited"), ESearchCase::IgnoreCase)) return ACM_Limited;
	if (S.Equals(TEXT("locked"),  ESearchCase::IgnoreCase)) return ACM_Locked;
	return ACM_Free;
}

void HandleSetPhysicsConstraintProperties(
	const FString& BlueprintPath, const FString& ComponentName,
	const FString& LinearXMotion, const FString& LinearYMotion, const FString& LinearZMotion, float LinearLimit,
	const FString& Swing1Motion, const FString& Swing2Motion, float Swing1Limit, float Swing2Limit,
	const FString& TwistMotion, float TwistLimit,
	FString& OutJsonString, FString& OutError)
{

	UBlueprint* BP = Cast<UBlueprint>(UEditorAssetLibrary::LoadAsset(BlueprintPath));
	if (!BP) { OutError = FString::Printf(TEXT("Cannot load Blueprint: %s"), *BlueprintPath); return; }

	USimpleConstructionScript* SCS = BP->SimpleConstructionScript;
	if (!SCS) { OutError = TEXT("Blueprint has no SimpleConstructionScript"); return; }

	USCS_Node* TargetNode = nullptr;
	for (USCS_Node* Node : SCS->GetAllNodes())
	{
		if (Node && Node->ComponentTemplate &&
			Node->GetVariableName().ToString().Equals(ComponentName, ESearchCase::IgnoreCase))
		{
			TargetNode = Node;
			break;
		}
	}
	if (!TargetNode) { OutError = FString::Printf(TEXT("Component '%s' not found in Blueprint"), *ComponentName); return; }

	UPhysicsConstraintComponent* Comp = Cast<UPhysicsConstraintComponent>(TargetNode->ComponentTemplate);
	if (!Comp) { OutError = FString::Printf(TEXT("Component '%s' is not a PhysicsConstraintComponent"), *ComponentName); return; }

	FConstraintInstance& CI = Comp->ConstraintInstance;
	TArray<FString> Changed;

	if (!LinearXMotion.IsEmpty()) { CI.SetLinearXMotion(ParseLinearMotion(LinearXMotion)); Changed.Add(TEXT("linear_x_motion")); }
	if (!LinearYMotion.IsEmpty()) { CI.SetLinearYMotion(ParseLinearMotion(LinearYMotion)); Changed.Add(TEXT("linear_y_motion")); }
	if (!LinearZMotion.IsEmpty()) { CI.SetLinearZMotion(ParseLinearMotion(LinearZMotion)); Changed.Add(TEXT("linear_z_motion")); }
	if (LinearLimit >= 0.0f)      { CI.SetLinearLimitSize(LinearLimit);                    Changed.Add(TEXT("linear_limit")); }

	if (!Swing1Motion.IsEmpty()) { CI.SetAngularSwing1Motion(ParseAngularMotion(Swing1Motion)); Changed.Add(TEXT("swing1_motion")); }
	if (!Swing2Motion.IsEmpty()) { CI.SetAngularSwing2Motion(ParseAngularMotion(Swing2Motion)); Changed.Add(TEXT("swing2_motion")); }
	if (!TwistMotion.IsEmpty())  { CI.SetAngularTwistMotion(ParseAngularMotion(TwistMotion));   Changed.Add(TEXT("twist_motion")); }
	if (Swing1Limit >= 0.0f)     { CI.SetAngularSwing1Limit(ParseAngularMotion(Swing1Motion.IsEmpty() ? TEXT("limited") : Swing1Motion), Swing1Limit); Changed.Add(TEXT("swing1_limit")); }
	if (Swing2Limit >= 0.0f)     { CI.SetAngularSwing2Limit(ParseAngularMotion(Swing2Motion.IsEmpty() ? TEXT("limited") : Swing2Motion), Swing2Limit); Changed.Add(TEXT("swing2_limit")); }
	if (TwistLimit >= 0.0f)      { CI.SetAngularTwistLimit(ParseAngularMotion(TwistMotion.IsEmpty() ? TEXT("limited") : TwistMotion), TwistLimit);    Changed.Add(TEXT("twist_limit")); }

	FBlueprintEditorUtils::MarkBlueprintAsModified(BP);
	BP->MarkPackageDirty();
	UEditorAssetLibrary::SaveAsset(BlueprintPath, false);

	FString ChangedStr;
	for (int32 i = 0; i < Changed.Num(); ++i) { if (i > 0) ChangedStr += TEXT(", "); ChangedStr += Changed[i]; }
	OutJsonString = FString::Printf(TEXT("{\"success\":true,\"component\":\"%s\",\"changed\":\"%s\"}"), *ComponentName, *ChangedStr);
}

static ECollisionChannel SlotIndexToChannel(int32 Index)
{
	return (ECollisionChannel)(ECC_GameTraceChannel1 + (Index - 1));
}

void HandleCreateCollisionChannel(const FString& ChannelName, const FString& DefaultResponse,
	bool bIsTraceChannel, FString& OutJsonString, FString& OutError)
{
	if (ChannelName.IsEmpty()) { OutError = TEXT("channel_name is required"); return; }

	const FString IniSection = TEXT("/Script/Engine.CollisionProfile");
	const FString ProjectIniPath = FPaths::ProjectConfigDir() / TEXT("DefaultEngine.ini");
	TArray<FString> Existing;
	GConfig->GetArray(*IniSection, TEXT("+DefaultChannelResponses"), Existing, GEngineIni);
	TArray<FString> Existing2;
	GConfig->GetArray(*IniSection, TEXT("DefaultChannelResponses"), Existing2, GEngineIni);
	Existing.Append(Existing2);
	TArray<FString> ProjExisting;
	GConfig->GetArray(*IniSection, TEXT("DefaultChannelResponses"), ProjExisting, ProjectIniPath);
	Existing.Append(ProjExisting);

	for (const FString& E : Existing)
	{
		if (E.Contains(FString::Printf(TEXT("Name=\"%s\""), *ChannelName)))
		{
			OutError = FString::Printf(TEXT("Collision channel '%s' already exists"), *ChannelName);
			return;
		}
	}

	int32 FreeSlot = -1;
	for (int32 i = 1; i <= 18; i++)
	{
		FString SlotStr = FString::Printf(TEXT("ECC_GameTraceChannel%d"), i);
		bool bTaken = false;
		for (const FString& E : Existing)
		{
			if (E.Contains(SlotStr)) { bTaken = true; break; }
		}
		if (!bTaken) { FreeSlot = i; break; }
	}
	if (FreeSlot < 0) { OutError = TEXT("No free GameTraceChannel slots (max 18 custom channels)"); return; }

	FString SlotStr = FString::Printf(TEXT("ECC_GameTraceChannel%d"), FreeSlot);
	FString ECRStr = TEXT("ECR_Block");
	if (DefaultResponse.Equals(TEXT("Overlap"), ESearchCase::IgnoreCase))     ECRStr = TEXT("ECR_Overlap");
	else if (DefaultResponse.Equals(TEXT("Ignore"), ESearchCase::IgnoreCase)) ECRStr = TEXT("ECR_Ignore");

	FString Entry = FString::Printf(
		TEXT("(Channel=%s,DefaultResponse=%s,bTraceType=%s,bStaticObject=False,Name=\"%s\")"),
		*SlotStr, *ECRStr, bIsTraceChannel ? TEXT("True") : TEXT("False"), *ChannelName);

	TArray<FString> WriteArr;
	GConfig->GetArray(*IniSection, TEXT("DefaultChannelResponses"), WriteArr, ProjectIniPath);
	WriteArr.Add(Entry);
	GConfig->SetArray(*IniSection, TEXT("DefaultChannelResponses"), WriteArr, ProjectIniPath);
	GConfig->Flush(false, ProjectIniPath);

	OutJsonString = FString::Printf(
		TEXT("{\"success\":true,\"channel_name\":\"%s\",\"slot\":\"%s\",\"slot_index\":%d,\"default_response\":\"%s\",\"note\":\"Editor restart required to apply channel\"}"),
		*ChannelName, *SlotStr, FreeSlot, *ECRStr);
	UE_LOG(LogMCPTool, Log, TEXT("create_collision_channel: '%s' → %s"), *ChannelName, *SlotStr);
}

void HandleListCollisionChannels(FString& OutJsonString, FString& OutError)
{
	const FString IniSection = TEXT("/Script/Engine.CollisionProfile");
	TArray<FString> Entries;
	GConfig->GetArray(*IniSection, TEXT("DefaultChannelResponses"), Entries, GEngineIni);
	const FString ProjectIniPath = FPaths::ProjectConfigDir() / TEXT("DefaultEngine.ini");
	TArray<FString> ProjEntries;
	GConfig->GetArray(*IniSection, TEXT("DefaultChannelResponses"), ProjEntries, ProjectIniPath);
	for (const FString& PE : ProjEntries)
	{
		bool bDup = false;
		for (const FString& E : Entries) { if (E.Equals(PE)) { bDup = true; break; } }
		if (!bDup) Entries.Add(PE);
	}

	TArray<TSharedPtr<FJsonValue>> ChannelArr;

	struct FBuiltIn { const TCHAR* Name; const TCHAR* Slot; };
	static const FBuiltIn BuiltIns[] = {
		{TEXT("WorldStatic"),   TEXT("ECC_WorldStatic")},
		{TEXT("WorldDynamic"),  TEXT("ECC_WorldDynamic")},
		{TEXT("Pawn"),          TEXT("ECC_Pawn")},
		{TEXT("Visibility"),    TEXT("ECC_Visibility")},
		{TEXT("Camera"),        TEXT("ECC_Camera")},
		{TEXT("PhysicsBody"),   TEXT("ECC_PhysicsBody")},
		{TEXT("Vehicle"),       TEXT("ECC_Vehicle")},
		{TEXT("Destructible"),  TEXT("ECC_Destructible")},
	};
	for (const auto& BI : BuiltIns)
	{
		TSharedPtr<FJsonObject> C = MakeShared<FJsonObject>();
		C->SetStringField(TEXT("name"), BI.Name);
		C->SetStringField(TEXT("slot"), BI.Slot);
		C->SetStringField(TEXT("type"), TEXT("builtin"));
		ChannelArr.Add(MakeShared<FJsonValueObject>(C));
	}

	for (const FString& E : Entries)
	{
		FString Slot, Response, Name, TraceType;
		TArray<FString> Parts;
		FString Clean = E.TrimStartAndEnd().TrimChar('(').TrimChar(')');
		Clean.ParseIntoArray(Parts, TEXT(","), false);
		for (const FString& P : Parts)
		{
			FString Key, Val;
			if (P.Split(TEXT("="), &Key, &Val))
			{
				Key.TrimStartAndEndInline(); Val.TrimStartAndEndInline();
				Val = Val.TrimChar('"');
				if (Key == TEXT("Channel"))            Slot = Val;
				else if (Key == TEXT("DefaultResponse")) Response = Val;
				else if (Key == TEXT("bTraceType"))    TraceType = Val;
				else if (Key == TEXT("Name"))           Name = Val;
			}
		}
		if (Name.IsEmpty() || Slot.IsEmpty()) continue;
		TSharedPtr<FJsonObject> C = MakeShared<FJsonObject>();
		C->SetStringField(TEXT("name"), Name);
		C->SetStringField(TEXT("slot"), Slot);
		C->SetStringField(TEXT("default_response"), Response);
		C->SetStringField(TEXT("type"), TraceType.Equals(TEXT("True"), ESearchCase::IgnoreCase) ? TEXT("trace") : TEXT("object"));
		ChannelArr.Add(MakeShared<FJsonValueObject>(C));
	}

	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetBoolField(TEXT("success"), true);
	Root->SetNumberField(TEXT("count"), ChannelArr.Num());
	Root->SetArrayField(TEXT("channels"), ChannelArr);

	FString Out;
	TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&Out);
	FJsonSerializer::Serialize(Root.ToSharedRef(), W);
	OutJsonString = Out;
}

static bool OpenGCInEditor(const FString& AssetPath, FString& OutError)
{
	UObject* Asset = UEditorAssetLibrary::LoadAsset(AssetPath);
	if (!Asset) { OutError = FString::Printf(TEXT("GeometryCollection not found: %s"), *AssetPath); return false; }
	UClass* GCClass = FindGeometryCollectionClass();
	if (!GCClass || !Asset->IsA(GCClass)) { OutError = FString::Printf(TEXT("'%s' is not a GeometryCollection"), *AssetPath); return false; }

	if (UAssetEditorSubsystem* AES = GEditor ? GEditor->GetEditorSubsystem<UAssetEditorSubsystem>() : nullptr)
		AES->OpenEditorForAsset(Asset);

	return true;
}

static FString FractureNotImplementedNote(const FString& FractureType)
{
	return FString::Printf(
		TEXT("Programmatic %s fracture is not yet implemented, so no fracture was applied to the GeometryCollection. The asset was opened in Fracture Mode (Shift+5) for you to fracture manually: select the GC, choose the fracture method, set the parameters, click Fracture. (Headless FFractureEngine support is a tracked follow-up that needs new module dependencies.)"),
		*FractureType);
}

void HandleFractureUniformFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError)
{
	const TArray<TSharedPtr<FJsonValue>>* ItemsArray = nullptr;
	if (BatchToolHelper::TryGetBatchItems(Args, TEXT("geometries"), ItemsArray))
	{
		BatchToolHelper::FBatchResultBuilder Batch;
		for (int32 i = 0; i < ItemsArray->Num(); i++)
		{
			TSharedPtr<FJsonObject> Item = (*ItemsArray)[i]->AsObject();
			if (!Item.IsValid()) { Batch.AddFailure(i, TEXT("Invalid item")); continue; }
			FString GCPath = BatchToolHelper::GetItemString(Item, TEXT("geometry_collection_path"), TEXT("gc_path"));
			FString Err;
			if (!OpenGCInEditor(GCPath, Err)) { Batch.AddFailure(i, Err); continue; }
			Batch.AddFailure(i, FractureNotImplementedNote(TEXT("uniform-grid")));
		}
		Batch.Finalize(OutJson);
		return;
	}
	FString GCPath;
	Args->TryGetStringField(TEXT("geometry_collection_path"), GCPath);
	if (!OpenGCInEditor(GCPath, OutError)) return;
	OutError = FractureNotImplementedNote(TEXT("uniform-grid"));
}

void HandleFractureVoronoiFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError)
{
	const TArray<TSharedPtr<FJsonValue>>* ItemsArray = nullptr;
	if (BatchToolHelper::TryGetBatchItems(Args, TEXT("geometries"), ItemsArray))
	{
		BatchToolHelper::FBatchResultBuilder Batch;
		for (int32 i = 0; i < ItemsArray->Num(); i++)
		{
			TSharedPtr<FJsonObject> Item = (*ItemsArray)[i]->AsObject();
			if (!Item.IsValid()) { Batch.AddFailure(i, TEXT("Invalid item")); continue; }
			FString GCPath = BatchToolHelper::GetItemString(Item, TEXT("geometry_collection_path"), TEXT("gc_path"));
			FString Err;
			if (!OpenGCInEditor(GCPath, Err)) { Batch.AddFailure(i, Err); continue; }
			Batch.AddFailure(i, FractureNotImplementedNote(TEXT("voronoi")));
		}
		Batch.Finalize(OutJson);
		return;
	}
	FString GCPath;
	Args->TryGetStringField(TEXT("geometry_collection_path"), GCPath);
	if (!OpenGCInEditor(GCPath, OutError)) return;
	OutError = FractureNotImplementedNote(TEXT("voronoi"));
}

void HandleFractureClusteredFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError)
{
	const TArray<TSharedPtr<FJsonValue>>* ItemsArray = nullptr;
	if (BatchToolHelper::TryGetBatchItems(Args, TEXT("geometries"), ItemsArray))
	{
		BatchToolHelper::FBatchResultBuilder Batch;
		for (int32 i = 0; i < ItemsArray->Num(); i++)
		{
			TSharedPtr<FJsonObject> Item = (*ItemsArray)[i]->AsObject();
			if (!Item.IsValid()) { Batch.AddFailure(i, TEXT("Invalid item")); continue; }
			FString GCPath = BatchToolHelper::GetItemString(Item, TEXT("geometry_collection_path"), TEXT("gc_path"));
			FString Err;
			if (!OpenGCInEditor(GCPath, Err)) { Batch.AddFailure(i, Err); continue; }
			Batch.AddFailure(i, FractureNotImplementedNote(TEXT("clustered-voronoi")));
		}
		Batch.Finalize(OutJson);
		return;
	}
	FString GCPath;
	Args->TryGetStringField(TEXT("geometry_collection_path"), GCPath);
	if (!OpenGCInEditor(GCPath, OutError)) return;
	OutError = FractureNotImplementedNote(TEXT("clustered-voronoi"));
}

void HandleSetFractureAutoClusterFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError)
{
	const TArray<TSharedPtr<FJsonValue>>* ItemsArray = nullptr;
	if (BatchToolHelper::TryGetBatchItems(Args, TEXT("geometries"), ItemsArray))
	{
		BatchToolHelper::FBatchResultBuilder Batch;
		for (int32 i = 0; i < ItemsArray->Num(); i++)
		{
			TSharedPtr<FJsonObject> Item = (*ItemsArray)[i]->AsObject();
			if (!Item.IsValid()) { Batch.AddFailure(i, TEXT("Invalid item")); continue; }
			FString GCPath = BatchToolHelper::GetItemString(Item, TEXT("geometry_collection_path"), TEXT("gc_path"));
			double ML = -1; double CG = -1;
			Item->TryGetNumberField(TEXT("max_cluster_level"), ML);
			Item->TryGetNumberField(TEXT("cluster_group_index"), CG);
			FString ItemOut, ItemErr;
			UObject* Asset = UEditorAssetLibrary::LoadAsset(GCPath);
			UClass* GCClass = FindGeometryCollectionClass();
			if (!Asset || !GCClass || !Asset->IsA(GCClass)) { Batch.AddFailure(i, FString::Printf(TEXT("GC not found: %s"), *GCPath)); continue; }
			if (FBoolProperty* BP2 = FindFProperty<FBoolProperty>(GCClass, TEXT("EnableClustering")))
				BP2->SetPropertyValue_InContainer(Asset, true);
			if (ML >= 0) if (FIntProperty* IP = FindFProperty<FIntProperty>(GCClass, TEXT("MaxClusterLevel")))
				IP->SetPropertyValue_InContainer(Asset, (int32)ML);
			if (CG >= 0) if (FIntProperty* IP = FindFProperty<FIntProperty>(GCClass, TEXT("ClusterGroupIndex")))
				IP->SetPropertyValue_InContainer(Asset, (int32)CG);
			Asset->MarkPackageDirty();
			TSharedPtr<FJsonObject> E = MakeShared<FJsonObject>();
			E->SetStringField(TEXT("geometry_collection_path"), GCPath);
			E->SetBoolField(TEXT("enable_clustering"), true);
			Batch.AddSuccess(i, E);
		}
		Batch.Finalize(OutJson);
		return;
	}
	FString GCPath; double ML = -1, CG = -1;
	Args->TryGetStringField(TEXT("geometry_collection_path"), GCPath);
	Args->TryGetNumberField(TEXT("max_cluster_level"), ML);
	Args->TryGetNumberField(TEXT("cluster_group_index"), CG);
	if (GCPath.IsEmpty()) { OutError = TEXT("geometry_collection_path required"); return; }
	UObject* Asset = UEditorAssetLibrary::LoadAsset(GCPath);
	UClass* GCClass = FindGeometryCollectionClass();
	if (!Asset || !GCClass || !Asset->IsA(GCClass)) { OutError = FString::Printf(TEXT("GC not found: %s"), *GCPath); return; }
	if (FBoolProperty* BP2 = FindFProperty<FBoolProperty>(GCClass, TEXT("EnableClustering")))
		BP2->SetPropertyValue_InContainer(Asset, true);
	if (ML >= 0) if (FIntProperty* IP = FindFProperty<FIntProperty>(GCClass, TEXT("MaxClusterLevel")))
		IP->SetPropertyValue_InContainer(Asset, (int32)ML);
	if (CG >= 0) if (FIntProperty* IP = FindFProperty<FIntProperty>(GCClass, TEXT("ClusterGroupIndex")))
		IP->SetPropertyValue_InContainer(Asset, (int32)CG);
	Asset->MarkPackageDirty();
	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetBoolField(TEXT("success"), true);
	Root->SetStringField(TEXT("geometry_collection_path"), GCPath);
	Root->SetBoolField(TEXT("enable_clustering"), true);
	if (ML >= 0) Root->SetNumberField(TEXT("max_cluster_level"), (int32)ML);
	if (CG >= 0) Root->SetNumberField(TEXT("cluster_group_index"), (int32)CG);
	TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&OutJson);
	FJsonSerializer::Serialize(Root.ToSharedRef(), W);
}

void HandleGetFractureSummaryFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError)
{
	const TArray<TSharedPtr<FJsonValue>>* ItemsArray = nullptr;
	if (BatchToolHelper::TryGetBatchItems(Args, TEXT("paths"), ItemsArray))
	{
		BatchToolHelper::FBatchResultBuilder Batch;
		for (int32 i = 0; i < ItemsArray->Num(); i++)
		{
			FString Path;
			if ((*ItemsArray)[i]->Type == EJson::String)
				Path = (*ItemsArray)[i]->AsString();
			else if (TSharedPtr<FJsonObject> ItemObj = (*ItemsArray)[i]->AsObject())
				ItemObj->TryGetStringField(TEXT("geometry_collection_path"), Path);

			if (Path.IsEmpty()) { Batch.AddFailure(i, TEXT("path required")); continue; }
			TSharedPtr<FJsonObject> SingleArg = MakeShared<FJsonObject>();
			SingleArg->SetStringField(TEXT("asset_path"), Path);
			FString ItemOut, ItemErr;
			HandleGetGCSummaryFromArgs(SingleArg, ItemOut, ItemErr);
			if (!ItemErr.IsEmpty()) { Batch.AddFailure(i, ItemErr); continue; }
			TSharedPtr<FJsonObject> Parsed;
			TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(ItemOut);
			if (FJsonSerializer::Deserialize(Reader, Parsed) && Parsed.IsValid())
				Batch.AddSuccess(i, Parsed);
			else
				Batch.AddFailure(i, TEXT("Failed to parse summary"));
		}
		Batch.Finalize(OutJson);
		return;
	}
	HandleGetGCSummaryFromArgs(Args, OutJson, OutError);
}

void HandleCreatePhysicsAssetFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString SkelMeshPath, SavePath;
	if (!Args->TryGetStringField(TEXT("skeletal_mesh_path"), SkelMeshPath))
	{
		OutError = TEXT("skeletal_mesh_path required");
		return;
	}
	Args->TryGetStringField(TEXT("save_path"), SavePath);
	if (SavePath.IsEmpty()) SavePath = FSettingsManager::GetDefaultSavePath();
	HandleCreatePhysicsAsset(SkelMeshPath, SavePath, OutJsonString, OutError);
}

void HandleGetPhysicsAssetSummaryFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString AssetPath;
	if (!Args->TryGetStringField(TEXT("asset_path"), AssetPath))
	{
		OutError = TEXT("asset_path required");
		return;
	}
	HandleGetPhysicsAssetSummary(AssetPath, OutJsonString, OutError);
}

void HandleSetCollisionPresetFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString BP, CN, PN;
	Args->TryGetStringField(TEXT("blueprint_path"), BP);
	Args->TryGetStringField(TEXT("component_name"), CN);
	Args->TryGetStringField(TEXT("preset_name"), PN);
	HandleSetCollisionPreset(BP, CN, PN, OutJsonString, OutError);
}

void HandleSetCollisionResponseFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString BP, CN, Channel, Response;
	Args->TryGetStringField(TEXT("blueprint_path"), BP);
	Args->TryGetStringField(TEXT("component_name"), CN);
	Args->TryGetStringField(TEXT("channel"), Channel);
	Args->TryGetStringField(TEXT("response"), Response);
	HandleSetCollisionResponse(BP, CN, Channel, Response, OutJsonString, OutError);
}

void HandleGetCollisionInfoFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString BP, CN;
	Args->TryGetStringField(TEXT("blueprint_path"), BP);
	Args->TryGetStringField(TEXT("component_name"), CN);
	HandleGetCollisionInfo(BP, CN, OutJsonString, OutError);
}

void HandleSetCollisionEnabledFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString BP, CN, CM;
	bool bOE = false;
	Args->TryGetStringField(TEXT("blueprint_path"), BP);
	Args->TryGetStringField(TEXT("component_name"), CN);
	Args->TryGetStringField(TEXT("collision_mode"), CM);
	Args->TryGetBoolField(TEXT("generate_overlap_events"), bOE);
	const bool bOS = Args->HasField(TEXT("generate_overlap_events"));
	HandleSetCollisionEnabled(BP, CN, CM, bOE, bOS, OutJsonString, OutError);
}

void HandleCreateGeometryCollectionFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString Name, SavePath, MeshPath;
	Args->TryGetStringField(TEXT("name"), Name);
	Args->TryGetStringField(TEXT("save_path"), SavePath);
	Args->TryGetStringField(TEXT("static_mesh_path"), MeshPath);
	HandleCreateGeometryCollection(Name, SavePath, MeshPath, OutJsonString, OutError);
}

void HandleSetGeometryCollectionPropertiesFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString AP;
	double DamageThreshold = -1, MaxClusterLevel = -1;
	bool bEnableClustering = false;
	Args->TryGetStringField(TEXT("asset_path"), AP);
	Args->TryGetNumberField(TEXT("damage_threshold"), DamageThreshold);
	Args->TryGetBoolField(TEXT("enable_clustering"), bEnableClustering);
	Args->TryGetNumberField(TEXT("max_cluster_level"), MaxClusterLevel);
	HandleSetGeometryCollectionProperties(AP, (float)DamageThreshold, bEnableClustering,
		(int32)MaxClusterLevel, OutJsonString, OutError);
}

void HandleSetPhysicsConstraintPropertiesFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString BP, CN, LX, LY, LZ, S1, S2, TW;
	double LL = -1, S1L = -1, S2L = -1, TL = -1;
	Args->TryGetStringField(TEXT("blueprint_path"), BP);
	Args->TryGetStringField(TEXT("component_name"), CN);
	Args->TryGetStringField(TEXT("linear_x_motion"), LX);
	Args->TryGetStringField(TEXT("linear_y_motion"), LY);
	Args->TryGetStringField(TEXT("linear_z_motion"), LZ);
	Args->TryGetNumberField(TEXT("linear_limit"), LL);
	Args->TryGetStringField(TEXT("swing1_motion"), S1);
	Args->TryGetStringField(TEXT("swing2_motion"), S2);
	Args->TryGetStringField(TEXT("twist_motion"), TW);
	Args->TryGetNumberField(TEXT("swing1_limit"), S1L);
	Args->TryGetNumberField(TEXT("swing2_limit"), S2L);
	Args->TryGetNumberField(TEXT("twist_limit"), TL);
	HandleSetPhysicsConstraintProperties(BP, CN, LX, LY, LZ, (float)LL, S1, S2,
		(float)S1L, (float)S2L, TW, (float)TL, OutJsonString, OutError);
}

void HandleCreateCollisionChannelFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString ChannelName, DefaultResponse;
	bool bIsTrace = false;
	Args->TryGetStringField(TEXT("channel_name"), ChannelName);
	Args->TryGetStringField(TEXT("default_response"), DefaultResponse);
	Args->TryGetBoolField(TEXT("is_trace_channel"), bIsTrace);
	HandleCreateCollisionChannel(ChannelName, DefaultResponse, bIsTrace, OutJsonString, OutError);
}

void HandleListCollisionChannelsFromArgs(const TSharedPtr<FJsonObject>& ,
	FString& OutJsonString, FString& OutError)
{
	HandleListCollisionChannels(OutJsonString, OutError);
}

}
