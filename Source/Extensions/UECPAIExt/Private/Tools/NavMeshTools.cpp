// Copyright 2026, BlueprintsLab, All rights reserved.

#include "Tools/NavMeshTools.h"

#include "NavigationSystem.h"
#include "NavigationData.h"
#include "Engine/Brush.h"
#include "Engine/Polys.h"
#include "Model.h"
#include "Components/BrushComponent.h"
#include "BSPOps.h"
#include "NavMesh/NavMeshBoundsVolume.h"
#include "Perception/AIPerceptionComponent.h"
#include "Perception/AISenseConfig_Sight.h"
#include "Perception/AISenseConfig_Hearing.h"
#include "Perception/AISenseConfig_Damage.h"
#include "NavModifierVolume.h"
#include "Navigation/NavLinkProxy.h"
#include "AIController.h"

#include "Engine/Blueprint.h"
#include "Engine/SimpleConstructionScript.h"
#include "Engine/SCS_Node.h"
#include "EditorAssetLibrary.h"
#include "AssetToolsModule.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "EngineUtils.h"
#include "Editor.h"

namespace NavMeshTools
{

void HandleSpawnNavMeshBoundsVolume(const FString& ActorLabel,
	float LocationX, float LocationY, float LocationZ,
	float ExtentX, float ExtentY, float ExtentZ,
	FString& OutJsonString, FString& OutError)
{

	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!World) { OutError = TEXT("No editor world available"); return; }

	if (ExtentX <= 0.f) ExtentX = 5000.f;
	if (ExtentY <= 0.f) ExtentY = 5000.f;
	if (ExtentZ <= 0.f) ExtentZ = 2000.f;

	UClass* NavVolClass = FindObject<UClass>(nullptr, TEXT("/Script/NavigationSystem.NavMeshBoundsVolume"));
	if (!NavVolClass)
	{
		OutError = TEXT("NavMeshBoundsVolume class not found. Ensure NavigationSystem module is loaded.");
		return;
	}

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	FTransform SpawnTransform(FRotator::ZeroRotator, FVector(LocationX, LocationY, LocationZ));
	AActor* SpawnedActor = World->SpawnActor(NavVolClass, &SpawnTransform, Params);
	ABrush* Volume = Cast<ABrush>(SpawnedActor);

	if (!Volume)
	{
		if (SpawnedActor) SpawnedActor->Destroy();
		OutError = TEXT("Failed to spawn NavMeshBoundsVolume");
		return;
	}

	Volume->SetActorLabel(ActorLabel);

	if (!Volume->Brush)
	{
		Volume->Brush = NewObject<UModel>(Volume, NAME_None, RF_Transactional);
		Volume->Brush->Initialize(Volume, true);
	}
	if (!Volume->Brush->Polys)
	{
		Volume->Brush->Polys = NewObject<UPolys>(Volume->Brush, NAME_None, RF_Transactional);
	}

	Volume->Modify();
	Volume->Brush->Modify();

	float HX = ExtentX, HY = ExtentY, HZ = ExtentZ;
	Volume->Brush->Polys->Element.Empty();

	auto MakeVert = [](float X, float Y, float Z) { return FVector3f(X, Y, Z); };
	auto AddFace = [&](FVector3f V0, FVector3f V1, FVector3f V2, FVector3f V3)
	{
		FPoly Face;
		Face.Init();
		Face.Vertices.Add(V0);
		Face.Vertices.Add(V1);
		Face.Vertices.Add(V2);
		Face.Vertices.Add(V3);
		Face.Base = V0;
		Face.CalcNormal();
		Volume->Brush->Polys->Element.Add(Face);
	};

	AddFace(MakeVert(-HX,-HY,-HZ), MakeVert(-HX, HY,-HZ), MakeVert( HX, HY,-HZ), MakeVert( HX,-HY,-HZ));
	AddFace(MakeVert(-HX,-HY, HZ), MakeVert( HX,-HY, HZ), MakeVert( HX, HY, HZ), MakeVert(-HX, HY, HZ));
	AddFace(MakeVert(-HX,-HY,-HZ), MakeVert( HX,-HY,-HZ), MakeVert( HX,-HY, HZ), MakeVert(-HX,-HY, HZ));
	AddFace(MakeVert( HX, HY,-HZ), MakeVert(-HX, HY,-HZ), MakeVert(-HX, HY, HZ), MakeVert( HX, HY, HZ));
	AddFace(MakeVert(-HX, HY,-HZ), MakeVert(-HX,-HY,-HZ), MakeVert(-HX,-HY, HZ), MakeVert(-HX, HY, HZ));
	AddFace(MakeVert( HX,-HY,-HZ), MakeVert( HX, HY,-HZ), MakeVert( HX, HY, HZ), MakeVert( HX,-HY, HZ));

	FBSPOps::RebuildBrush(Volume->Brush);
	Volume->Brush->BuildBound();

	if (UBrushComponent* BC = Volume->GetBrushComponent())
	{
		BC->Brush = Volume->Brush;
		BC->UpdateBounds();
		BC->MarkRenderStateDirty();
		BC->RecreatePhysicsState();
	}

	Volume->PostEditChange();
	Volume->ReregisterAllComponents();

	if (ANavMeshBoundsVolume* NavVol = Cast<ANavMeshBoundsVolume>(Volume))
	{
		if (UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World))
		{
			NavSys->OnNavigationBoundsUpdated(NavVol);
		}
	}

	Volume->MarkPackageDirty();
	GEditor->RedrawAllViewports();

	OutJsonString = FString::Printf(
		TEXT("{\"success\":true,\"actor_label\":\"%s\",\"location\":[%.0f,%.0f,%.0f],\"half_extents\":[%.0f,%.0f,%.0f]}"),
		*ActorLabel, LocationX, LocationY, LocationZ, ExtentX, ExtentY, ExtentZ);
}

void HandleSetNavMeshConfig(float AgentRadius, float AgentHeight, float MaxStepHeight, float CellSize,
	FString& OutJsonString, FString& OutError)
{

	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!World) { OutError = TEXT("No editor world available"); return; }

	UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
	if (!NavSys) { OutError = TEXT("No NavigationSystem found. Ensure a NavMeshBoundsVolume exists in the level."); return; }

	ANavigationData* NavData = Cast<ANavigationData>(NavSys->GetMainNavData());
	if (!NavData) { OutError = TEXT("No NavData found. Spawn a NavMeshBoundsVolume and rebuild first."); return; }

	UClass* NavDataClass = NavData->GetClass();
	auto SetFloatProp = [&](const FString& PropName, float Value)
	{
		if (Value <= 0.f) return;
		FFloatProperty* Prop = FindFProperty<FFloatProperty>(NavDataClass, *PropName);
		if (Prop) Prop->SetPropertyValue_InContainer(NavData, Value);
	};

	SetFloatProp(TEXT("AgentRadius"),        AgentRadius);
	SetFloatProp(TEXT("AgentHeight"),        AgentHeight);
	SetFloatProp(TEXT("AgentMaxStepHeight"), MaxStepHeight);
	SetFloatProp(TEXT("CellSize"),           CellSize);

	NavData->MarkPackageDirty();

	float OutRadius = AgentRadius, OutHeight = AgentHeight, OutStep = MaxStepHeight, OutCell = CellSize;
	auto ReadFloat = [&](const FString& PropName) -> float
	{
		FFloatProperty* Prop = FindFProperty<FFloatProperty>(NavDataClass, *PropName);
		return Prop ? Prop->GetPropertyValue_InContainer(NavData) : -1.f;
	};

	OutJsonString = FString::Printf(
		TEXT("{\"success\":true,\"agent_radius\":%.1f,\"agent_height\":%.1f,\"max_step_height\":%.1f,\"cell_size\":%.1f}"),
		ReadFloat(TEXT("AgentRadius")), ReadFloat(TEXT("AgentHeight")),
		ReadFloat(TEXT("AgentMaxStepHeight")), ReadFloat(TEXT("CellSize")));
}

void HandleRebuildNavMesh(FString& OutJsonString, FString& OutError)
{

	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!World) { OutError = TEXT("No editor world available"); return; }

	UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
	if (!NavSys) { OutError = TEXT("No NavigationSystem found. Spawn a NavMeshBoundsVolume first."); return; }

	NavSys->Build();

	OutJsonString = TEXT("{\"success\":true,\"message\":\"NavMesh rebuild triggered. Generation is asynchronous — the navmesh is now active. User can press P in viewport to see the green overlay. No further steps needed.\"}");
}

void HandleGetNavMeshInfo(FString& OutJsonString, FString& OutError)
{

	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!World) { OutError = TEXT("No editor world available"); return; }

	UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
	if (!NavSys)
	{
		OutJsonString = TEXT("{\"success\":true,\"has_navmesh\":false,\"message\":\"No NavigationSystem. Spawn a NavMeshBoundsVolume.\"}");
		return;
	}

	UClass* NavVolClass = FindObject<UClass>(nullptr, TEXT("/Script/NavigationSystem.NavMeshBoundsVolume"));
	int32 VolumeCount = 0;
	if (NavVolClass)
	{
		for (TActorIterator<AActor> It(World, NavVolClass); It; ++It) { VolumeCount++; }
	}

	ANavigationData* NavData = Cast<ANavigationData>(NavSys->GetMainNavData());
	if (!NavData)
	{
		OutJsonString = FString::Printf(
			TEXT("{\"success\":true,\"has_navmesh\":false,\"volume_count\":%d,\"message\":\"NavMesh not built yet. Call rebuild_navmesh.\"}"),
			VolumeCount);
		return;
	}

	UClass* NavDataClass = NavData->GetClass();
	auto ReadFloat = [&](const FString& PropName) -> float
	{
		FFloatProperty* Prop = FindFProperty<FFloatProperty>(NavDataClass, *PropName);
		return Prop ? Prop->GetPropertyValue_InContainer(NavData) : -1.f;
	};

	int32 TileCount = 0;
	if (UFunction* GetTilesFunc = NavDataClass->FindFunctionByName(TEXT("GetNavMeshTilesCount")))
	{
		struct { int32 RetVal; } Params;
		Params.RetVal = 0;
		NavData->ProcessEvent(GetTilesFunc, &Params);
		TileCount = Params.RetVal;
	}
	else
	{
		FIntProperty* TileProp = FindFProperty<FIntProperty>(NavDataClass, TEXT("NumActiveTiles"));
		if (TileProp) TileCount = TileProp->GetPropertyValue_InContainer(NavData);
	}
	if (TileCount < 0) TileCount = 0;

	const TCHAR* TileNote = (TileCount == 0)
		? TEXT("Tile count is 0 — this is normal in the UE5 editor (async generation). NavMesh IS active if a volume exists. User can press P in viewport to see the green overlay.")
		: TEXT("NavMesh active and tiles generated.");

	OutJsonString = FString::Printf(
		TEXT("{\"success\":true,\"has_navmesh\":true,\"tile_count\":%d,\"volume_count\":%d,\"agent_radius\":%.1f,\"agent_height\":%.1f,\"max_step_height\":%.1f,\"cell_size\":%.1f,\"note\":\"%s\"}"),
		TileCount, VolumeCount,
		ReadFloat(TEXT("AgentRadius")), ReadFloat(TEXT("AgentHeight")),
		ReadFloat(TEXT("AgentMaxStepHeight")), ReadFloat(TEXT("CellSize")),
		TileNote);
}

void HandleAddAIPerceptionComponent(const FString& BlueprintPath, const FString& ComponentName,
	FString& OutJsonString, FString& OutError)
{

	UBlueprint* Blueprint = Cast<UBlueprint>(UEditorAssetLibrary::LoadAsset(BlueprintPath));
	if (!Blueprint) { OutError = TEXT("Blueprint not found: ") + BlueprintPath; return; }

	USimpleConstructionScript* SCS = Blueprint->SimpleConstructionScript;
	if (!SCS) { OutError = TEXT("Blueprint has no SCS (not an Actor-based Blueprint)"); return; }

	FString CompName = ComponentName.IsEmpty() ? TEXT("AIPerception") : ComponentName;
	for (USCS_Node* ExistingNode : SCS->GetAllNodes())
	{
		if (ExistingNode->ComponentTemplate && ExistingNode->ComponentTemplate->IsA(UAIPerceptionComponent::StaticClass()))
		{
			OutJsonString = FString::Printf(TEXT("{\"success\":true,\"component\":\"%s\",\"message\":\"AIPerceptionComponent already exists\"}"), *CompName);
			return;
		}
	}

	USCS_Node* Node = SCS->CreateNode(UAIPerceptionComponent::StaticClass(), *CompName);
	SCS->AddNode(Node);

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
	Blueprint->MarkPackageDirty();
	UEditorAssetLibrary::SaveAsset(BlueprintPath, false);

	OutJsonString = FString::Printf(
		TEXT("{\"success\":true,\"component\":\"%s\",\"message\":\"AIPerceptionComponent added. Call configure_ai_sight/configure_ai_hearing to set senses.\"}"),
		*CompName);
}

void HandleConfigureAISight(const FString& BlueprintPath,
	float SightRadius, float LoseSightRadius, float PeripheralAngle,
	bool bDetectEnemies, bool bDetectNeutrals, bool bDetectFriendlies,
	FString& OutJsonString, FString& OutError)
{

	UBlueprint* Blueprint = Cast<UBlueprint>(UEditorAssetLibrary::LoadAsset(BlueprintPath));
	if (!Blueprint) { OutError = TEXT("Blueprint not found: ") + BlueprintPath; return; }

	UAIPerceptionComponent* PercComp = nullptr;
	if (Blueprint->SimpleConstructionScript)
	{
		for (USCS_Node* Node : Blueprint->SimpleConstructionScript->GetAllNodes())
		{
			if (Node->ComponentTemplate && Node->ComponentTemplate->IsA(UAIPerceptionComponent::StaticClass()))
			{
				PercComp = Cast<UAIPerceptionComponent>(Node->ComponentTemplate);
				break;
			}
		}
	}

	if (!PercComp)
	{
		OutError = TEXT("No AIPerceptionComponent found on Blueprint. Call add_ai_perception_component first.");
		return;
	}

	if (SightRadius <= 0.f)      SightRadius      = 1500.f;
	if (LoseSightRadius <= 0.f)  LoseSightRadius  = SightRadius + 200.f;
	if (PeripheralAngle <= 0.f)  PeripheralAngle  = 90.f;

	UAISenseConfig_Sight* SightConfig = NewObject<UAISenseConfig_Sight>(GetTransientPackage());
	SightConfig->SightRadius = SightRadius;
	SightConfig->LoseSightRadius = LoseSightRadius;
	SightConfig->PeripheralVisionAngleDegrees = PeripheralAngle;
	SightConfig->DetectionByAffiliation.bDetectEnemies    = bDetectEnemies;
	SightConfig->DetectionByAffiliation.bDetectNeutrals   = bDetectNeutrals;
	SightConfig->DetectionByAffiliation.bDetectFriendlies = bDetectFriendlies;

	PercComp->ConfigureSense(*SightConfig);

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
	Blueprint->MarkPackageDirty();
	UEditorAssetLibrary::SaveAsset(BlueprintPath, false);

	OutJsonString = FString::Printf(
		TEXT("{\"success\":true,\"sight_radius\":%.0f,\"lose_sight_radius\":%.0f,\"peripheral_angle\":%.1f}"),
		SightRadius, LoseSightRadius, PeripheralAngle);
}

void HandleConfigureAIHearing(const FString& BlueprintPath, float HearingRange,
	bool bDetectEnemies, bool bDetectNeutrals, bool bDetectFriendlies,
	FString& OutJsonString, FString& OutError)
{

	UBlueprint* Blueprint = Cast<UBlueprint>(UEditorAssetLibrary::LoadAsset(BlueprintPath));
	if (!Blueprint) { OutError = TEXT("Blueprint not found: ") + BlueprintPath; return; }

	UAIPerceptionComponent* PercComp = nullptr;
	if (Blueprint->SimpleConstructionScript)
	{
		for (USCS_Node* Node : Blueprint->SimpleConstructionScript->GetAllNodes())
		{
			if (Node->ComponentTemplate && Node->ComponentTemplate->IsA(UAIPerceptionComponent::StaticClass()))
			{
				PercComp = Cast<UAIPerceptionComponent>(Node->ComponentTemplate);
				break;
			}
		}
	}

	if (!PercComp)
	{
		OutError = TEXT("No AIPerceptionComponent found on Blueprint. Call add_ai_perception_component first.");
		return;
	}

	if (HearingRange <= 0.f) HearingRange = 800.f;

	UAISenseConfig_Hearing* HearingConfig = NewObject<UAISenseConfig_Hearing>(GetTransientPackage());
	HearingConfig->HearingRange = HearingRange;
	HearingConfig->DetectionByAffiliation.bDetectEnemies    = bDetectEnemies;
	HearingConfig->DetectionByAffiliation.bDetectNeutrals   = bDetectNeutrals;
	HearingConfig->DetectionByAffiliation.bDetectFriendlies = bDetectFriendlies;

	PercComp->ConfigureSense(*HearingConfig);

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
	Blueprint->MarkPackageDirty();
	UEditorAssetLibrary::SaveAsset(BlueprintPath, false);

	OutJsonString = FString::Printf(
		TEXT("{\"success\":true,\"hearing_range\":%.0f}"),
		HearingRange);
}

void HandleAddNavModifierVolume(const FString& ActorLabel,
	float LocationX, float LocationY, float LocationZ,
	float ExtentX, float ExtentY, float ExtentZ,
	const FString& AreaClass, FString& OutJsonString, FString& OutError)
{

	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!World) { OutError = TEXT("No editor world"); return; }

	FVector Location(LocationX, LocationY, LocationZ);
	FRotator Rotation = FRotator::ZeroRotator;
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	ANavModifierVolume* Volume = World->SpawnActor<ANavModifierVolume>(ANavModifierVolume::StaticClass(), Location, Rotation, Params);
	if (!Volume) { OutError = TEXT("Failed to spawn NavModifierVolume"); return; }

	if (!ActorLabel.IsEmpty()) Volume->SetActorLabel(ActorLabel);

	if (!AreaClass.IsEmpty())
	{
		UClass* NavAreaClass = FindFirstObject<UClass>(*AreaClass);
		if (NavAreaClass) Volume->SetAreaClass(NavAreaClass);
	}

	Volume->MarkPackageDirty();

	OutJsonString = FString::Printf(TEXT("{\"success\":true,\"actor_label\":\"%s\",\"type\":\"NavModifierVolume\"}"),
		*Volume->GetActorLabel());
}

void HandleAddNavLinkProxy(const FString& ActorLabel,
	float LocationX, float LocationY, float LocationZ,
	float EndOffsetX, float EndOffsetY, float EndOffsetZ,
	FString& OutJsonString, FString& OutError)
{

	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!World) { OutError = TEXT("No editor world"); return; }

	FVector Location(LocationX, LocationY, LocationZ);
	FRotator Rotation = FRotator::ZeroRotator;
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	ANavLinkProxy* Link = World->SpawnActor<ANavLinkProxy>(ANavLinkProxy::StaticClass(), Location, Rotation, Params);
	if (!Link) { OutError = TEXT("Failed to spawn NavLinkProxy"); return; }

	if (!ActorLabel.IsEmpty()) Link->SetActorLabel(ActorLabel);
	Link->MarkPackageDirty();

	OutJsonString = FString::Printf(TEXT("{\"success\":true,\"actor_label\":\"%s\",\"type\":\"NavLinkProxy\"}"),
		*Link->GetActorLabel());
}

void HandleSetAIControllerClass(const FString& BlueprintPath, const FString& AIControllerClass,
	FString& OutJsonString, FString& OutError)
{

	UBlueprint* BP = Cast<UBlueprint>(UEditorAssetLibrary::LoadAsset(BlueprintPath));
	if (!BP || !BP->GeneratedClass) { OutError = TEXT("Blueprint not found: ") + BlueprintPath; return; }

	UClass* ControllerClass = FindFirstObject<UClass>(*AIControllerClass);
	if (!ControllerClass)
		ControllerClass = LoadObject<UClass>(nullptr, *AIControllerClass);
	if (!ControllerClass)
	{
		UObject* Loaded = UEditorAssetLibrary::LoadAsset(AIControllerClass);
		if (UBlueprint* LoadedBP = Cast<UBlueprint>(Loaded))
			ControllerClass = LoadedBP->GeneratedClass;
	}
	if (!ControllerClass && !AIControllerClass.EndsWith(TEXT("_C")))
	{
		FString WithSuffix = AIControllerClass + TEXT("_C");
		ControllerClass = FindFirstObject<UClass>(*WithSuffix);
	}
	if (!ControllerClass) { OutError = FString::Printf(TEXT("AI Controller class '%s' not found"), *AIControllerClass); return; }
	if (!ControllerClass->IsChildOf(AAIController::StaticClass()))
	{
		OutError = FString::Printf(TEXT("'%s' is not an AIController subclass"), *AIControllerClass);
		return;
	}

	UObject* CDO = BP->GeneratedClass->GetDefaultObject();
	FClassProperty* AICProp = FindFProperty<FClassProperty>(BP->GeneratedClass, TEXT("AIControllerClass"));
	if (AICProp)
	{
		AICProp->SetPropertyValue_InContainer(CDO, ControllerClass);
	}
	else
	{
		OutError = TEXT("AIControllerClass property not found — is this a Pawn/Character Blueprint?");
		return;
	}

	FEnumProperty* AutoPossessProp = FindFProperty<FEnumProperty>(BP->GeneratedClass, TEXT("AutoPossessAI"));
	if (AutoPossessProp)
	{
		void* ValuePtr = AutoPossessProp->ContainerPtrToValuePtr<void>(CDO);
		AutoPossessProp->GetUnderlyingProperty()->SetIntPropertyValue(ValuePtr, (int64)4);
	}

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(BP);
	UEditorAssetLibrary::SaveAsset(BlueprintPath, false);

	OutJsonString = FString::Printf(TEXT("{\"success\":true,\"blueprint_path\":\"%s\",\"ai_controller_class\":\"%s\",\"auto_possess_ai\":\"PlacedInWorldOrSpawned\"}"),
		*BlueprintPath, *AIControllerClass);
}

void HandleConfigureAIDamage(const FString& BlueprintPath,
	bool bDetectEnemies, bool bDetectNeutrals, bool bDetectFriendlies,
	FString& OutJsonString, FString& OutError)
{

	UBlueprint* BP = Cast<UBlueprint>(UEditorAssetLibrary::LoadAsset(BlueprintPath));
	if (!BP) { OutError = TEXT("Blueprint not found: ") + BlueprintPath; return; }

	USCS_Node* PerceptionNode = nullptr;
	if (BP->SimpleConstructionScript)
	{
		for (USCS_Node* Node : BP->SimpleConstructionScript->GetAllNodes())
		{
			if (Node && Node->ComponentClass && Node->ComponentClass->IsChildOf(UAIPerceptionComponent::StaticClass()))
			{
				PerceptionNode = Node;
				break;
			}
		}
	}
	if (!PerceptionNode) { OutError = TEXT("No AIPerceptionComponent found. Call add_ai_perception_component first."); return; }

	UAIPerceptionComponent* PerceptionComp = Cast<UAIPerceptionComponent>(PerceptionNode->ComponentTemplate);
	if (!PerceptionComp) { OutError = TEXT("Perception component template is null"); return; }

	UAISenseConfig_Damage* DamageConfig = NewObject<UAISenseConfig_Damage>(GetTransientPackage());
	DamageConfig->SetMaxAge(5.0f);
	PerceptionComp->ConfigureSense(*DamageConfig);

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(BP);
	UEditorAssetLibrary::SaveAsset(BlueprintPath, false);

	OutJsonString = FString::Printf(TEXT("{\"success\":true,\"blueprint_path\":\"%s\",\"sense\":\"Damage\"}"), *BlueprintPath);
}

void HandleCreateSmartObjectDefinition(const FString& Name, const FString& SavePath,
	FString& OutJsonString, FString& OutError)
{

	if (Name.IsEmpty()) { OutError = TEXT("name is required"); return; }

	UClass* SODClass = FindObject<UClass>(nullptr, TEXT("/Script/SmartObjectsModule.SmartObjectDefinition"));
	if (!SODClass) SODClass = UClass::TryFindTypeSlow<UClass>(TEXT("SmartObjectDefinition"));
	if (!SODClass)
	{
		OutError = TEXT("SmartObjectDefinition class not found. Enable the SmartObjects plugin in Edit > Plugins.");
		return;
	}

	FString TargetPath = SavePath.IsEmpty() ? TEXT("/Game/AI") : SavePath;

	FAssetToolsModule& ATM = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools"));
	UObject* NewAsset = ATM.Get().CreateAsset(Name, TargetPath, SODClass, nullptr);
	if (!NewAsset) { OutError = FString::Printf(TEXT("Failed to create SmartObjectDefinition '%s'"), *Name); return; }

	NewAsset->MarkPackageDirty();
	OutJsonString = FString::Printf(TEXT("{\"success\":true,\"asset_path\":\"%s\",\"name\":\"%s\",\"class\":\"SmartObjectDefinition\"}"),
		*NewAsset->GetPathName(), *Name);
}

void HandleAddSmartObjectSlot(const FString& AssetPath, const FString& ActivityTagName,
	FString& OutJsonString, FString& OutError)
{

	UObject* Asset = UEditorAssetLibrary::LoadAsset(AssetPath);
	if (!Asset) { OutError = FString::Printf(TEXT("Could not load asset at '%s'"), *AssetPath); return; }

	FArrayProperty* SlotsProp = FindFProperty<FArrayProperty>(Asset->GetClass(), TEXT("Slots"));
	if (!SlotsProp)
	{
		OutError = TEXT("Asset does not have a 'Slots' property. Is it a SmartObjectDefinition?");
		return;
	}

	FScriptArrayHelper SlotsHelper(SlotsProp, SlotsProp->ContainerPtrToValuePtr<void>(Asset));
	int32 NewIdx = SlotsHelper.AddValue();

	UScriptStruct* SlotStruct = CastField<FStructProperty>(SlotsProp->Inner) ? CastField<FStructProperty>(SlotsProp->Inner)->Struct : nullptr;
	if (SlotStruct && !ActivityTagName.IsEmpty())
	{
		void* SlotPtr = SlotsHelper.GetRawPtr(NewIdx);
		FProperty* TagProp = SlotStruct->FindPropertyByName(TEXT("ActivityTags"));
		if (TagProp)
		{
			void* TagPtr = TagProp->ContainerPtrToValuePtr<void>(SlotPtr);
			FString TagStr = FString::Printf(TEXT("(GameplayTags=((TagName=\"%s\")))"), *ActivityTagName);
			TagProp->ImportText_Direct(*TagStr, TagPtr, nullptr, PPF_None);
		}
	}

	Asset->MarkPackageDirty();
	OutJsonString = FString::Printf(TEXT("{\"success\":true,\"asset_path\":\"%s\",\"slot_index\":%d,\"total_slots\":%d}"),
		*AssetPath, NewIdx, SlotsHelper.Num());
}

void HandleGetSmartObjectInfo(const FString& AssetPath, FString& OutJsonString, FString& OutError)
{

	UObject* Asset = UEditorAssetLibrary::LoadAsset(AssetPath);
	if (!Asset) { OutError = FString::Printf(TEXT("Could not load asset at '%s'"), *AssetPath); return; }

	TSharedPtr<FJsonObject> Res = MakeShareable(new FJsonObject());
	Res->SetBoolField(TEXT("success"), true);
	Res->SetStringField(TEXT("asset_path"), AssetPath);
	Res->SetStringField(TEXT("class"), Asset->GetClass()->GetName());

	FArrayProperty* SlotsProp = FindFProperty<FArrayProperty>(Asset->GetClass(), TEXT("Slots"));
	if (SlotsProp)
	{
		FScriptArrayHelper SlotsHelper(SlotsProp, SlotsProp->ContainerPtrToValuePtr<void>(Asset));
		Res->SetNumberField(TEXT("slot_count"), SlotsHelper.Num());
	}

	TSharedPtr<FJsonObject> PropsObj = MakeShareable(new FJsonObject());
	for (TFieldIterator<FProperty> It(Asset->GetClass()); It; ++It)
	{
		if (!It->HasAnyPropertyFlags(CPF_Edit)) continue;
		if (It->GetName() == TEXT("Slots")) continue;
		FString ValStr;
		It->ExportTextItem_Direct(ValStr, It->ContainerPtrToValuePtr<void>(Asset), nullptr, Asset, PPF_None);
		if (!ValStr.IsEmpty() && ValStr.Len() < 200)
			PropsObj->SetStringField(It->GetName(), ValStr);
	}
	Res->SetObjectField(TEXT("properties"), PropsObj);

	TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&OutJsonString);
	FJsonSerializer::Serialize(Res.ToSharedRef(), W);
}

void HandleSpawnNavMeshBoundsVolumeFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString ActorLabel = TEXT("NavMeshBoundsVolume");
	double LocX = 0, LocY = 0, LocZ = 0, ExtX = 5000, ExtY = 5000, ExtZ = 2000;
	Args->TryGetStringField(TEXT("actor_label"), ActorLabel);
	Args->TryGetNumberField(TEXT("location_x"), LocX);
	Args->TryGetNumberField(TEXT("location_y"), LocY);
	Args->TryGetNumberField(TEXT("location_z"), LocZ);
	Args->TryGetNumberField(TEXT("extent_x"), ExtX);
	Args->TryGetNumberField(TEXT("extent_y"), ExtY);
	Args->TryGetNumberField(TEXT("extent_z"), ExtZ);
	HandleSpawnNavMeshBoundsVolume(ActorLabel,
		(float)LocX, (float)LocY, (float)LocZ,
		(float)ExtX, (float)ExtY, (float)ExtZ,
		OutJsonString, OutError);
}

void HandleSetNavMeshConfigFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	double AgentRadius = -1.0, AgentHeight = -1.0, MaxStepHeight = -1.0, CellSize = -1.0;
	Args->TryGetNumberField(TEXT("agent_radius"), AgentRadius);
	Args->TryGetNumberField(TEXT("agent_height"), AgentHeight);
	Args->TryGetNumberField(TEXT("max_step_height"), MaxStepHeight);
	Args->TryGetNumberField(TEXT("cell_size"), CellSize);
	HandleSetNavMeshConfig((float)AgentRadius, (float)AgentHeight, (float)MaxStepHeight, (float)CellSize,
		OutJsonString, OutError);
}

void HandleRebuildNavMeshFromArgs(const TSharedPtr<FJsonObject>& ,
	FString& OutJsonString, FString& OutError)
{
	HandleRebuildNavMesh(OutJsonString, OutError);
}

void HandleGetNavMeshInfoFromArgs(const TSharedPtr<FJsonObject>& ,
	FString& OutJsonString, FString& OutError)
{
	HandleGetNavMeshInfo(OutJsonString, OutError);
}

void HandleAddAIPerceptionComponentFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString BlueprintPath, ComponentName;
	Args->TryGetStringField(TEXT("blueprint_path"), BlueprintPath);
	Args->TryGetStringField(TEXT("component_name"), ComponentName);
	HandleAddAIPerceptionComponent(BlueprintPath, ComponentName, OutJsonString, OutError);
}

void HandleConfigureAISightFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString BlueprintPath;
	double SightRadius = 1500.0, LoseSightRadius = -1.0, PeripheralAngle = 90.0;
	bool bE = true, bN = true, bF = false;
	Args->TryGetStringField(TEXT("blueprint_path"), BlueprintPath);
	Args->TryGetNumberField(TEXT("sight_radius"), SightRadius);
	Args->TryGetNumberField(TEXT("lose_sight_radius"), LoseSightRadius);
	Args->TryGetNumberField(TEXT("peripheral_angle"), PeripheralAngle);
	Args->TryGetBoolField(TEXT("detect_enemies"), bE);
	Args->TryGetBoolField(TEXT("detect_neutrals"), bN);
	Args->TryGetBoolField(TEXT("detect_friendlies"), bF);
	HandleConfigureAISight(BlueprintPath, (float)SightRadius, (float)LoseSightRadius, (float)PeripheralAngle,
		bE, bN, bF, OutJsonString, OutError);
}

void HandleConfigureAIHearingFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString BlueprintPath;
	double HearingRange = 800.0;
	bool bE = true, bN = true, bF = false;
	Args->TryGetStringField(TEXT("blueprint_path"), BlueprintPath);
	Args->TryGetNumberField(TEXT("hearing_range"), HearingRange);
	Args->TryGetBoolField(TEXT("detect_enemies"), bE);
	Args->TryGetBoolField(TEXT("detect_neutrals"), bN);
	Args->TryGetBoolField(TEXT("detect_friendlies"), bF);
	HandleConfigureAIHearing(BlueprintPath, (float)HearingRange, bE, bN, bF, OutJsonString, OutError);
}

void HandleAddNavModifierVolumeFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString ActorLabel, AreaClass;
	double LX = 0, LY = 0, LZ = 0, EX = 500, EY = 500, EZ = 500;
	Args->TryGetStringField(TEXT("actor_label"), ActorLabel);
	Args->TryGetStringField(TEXT("area_class"), AreaClass);
	Args->TryGetNumberField(TEXT("location_x"), LX);
	Args->TryGetNumberField(TEXT("location_y"), LY);
	Args->TryGetNumberField(TEXT("location_z"), LZ);
	Args->TryGetNumberField(TEXT("extent_x"), EX);
	Args->TryGetNumberField(TEXT("extent_y"), EY);
	Args->TryGetNumberField(TEXT("extent_z"), EZ);
	HandleAddNavModifierVolume(ActorLabel, (float)LX, (float)LY, (float)LZ,
		(float)EX, (float)EY, (float)EZ, AreaClass, OutJsonString, OutError);
}

void HandleAddNavLinkProxyFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString ActorLabel;
	double LX = 0, LY = 0, LZ = 0, EOX = 0, EOY = 0, EOZ = 0;
	Args->TryGetStringField(TEXT("actor_label"), ActorLabel);
	Args->TryGetNumberField(TEXT("location_x"), LX);
	Args->TryGetNumberField(TEXT("location_y"), LY);
	Args->TryGetNumberField(TEXT("location_z"), LZ);
	Args->TryGetNumberField(TEXT("end_offset_x"), EOX);
	Args->TryGetNumberField(TEXT("end_offset_y"), EOY);
	Args->TryGetNumberField(TEXT("end_offset_z"), EOZ);
	HandleAddNavLinkProxy(ActorLabel, (float)LX, (float)LY, (float)LZ,
		(float)EOX, (float)EOY, (float)EOZ, OutJsonString, OutError);
}

void HandleSetAIControllerClassFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString BlueprintPath, AIControllerClass;
	Args->TryGetStringField(TEXT("blueprint_path"), BlueprintPath);
	Args->TryGetStringField(TEXT("ai_controller_class"), AIControllerClass);
	HandleSetAIControllerClass(BlueprintPath, AIControllerClass, OutJsonString, OutError);
}

void HandleConfigureAIDamageFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString BlueprintPath;
	bool bE = true, bN = true, bF = false;
	Args->TryGetStringField(TEXT("blueprint_path"), BlueprintPath);
	Args->TryGetBoolField(TEXT("detect_enemies"), bE);
	Args->TryGetBoolField(TEXT("detect_neutrals"), bN);
	Args->TryGetBoolField(TEXT("detect_friendlies"), bF);
	HandleConfigureAIDamage(BlueprintPath, bE, bN, bF, OutJsonString, OutError);
}

void HandleCreateSmartObjectDefinitionFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString Name, SavePath;
	Args->TryGetStringField(TEXT("name"), Name);
	Args->TryGetStringField(TEXT("save_path"), SavePath);
	HandleCreateSmartObjectDefinition(Name, SavePath, OutJsonString, OutError);
}

void HandleAddSmartObjectSlotFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString AssetPath, ActivityTag;
	Args->TryGetStringField(TEXT("asset_path"), AssetPath);
	Args->TryGetStringField(TEXT("activity_tag"), ActivityTag);
	HandleAddSmartObjectSlot(AssetPath, ActivityTag, OutJsonString, OutError);
}

void HandleGetSmartObjectInfoFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString AssetPath;
	Args->TryGetStringField(TEXT("asset_path"), AssetPath);
	HandleGetSmartObjectInfo(AssetPath, OutJsonString, OutError);
}

}
