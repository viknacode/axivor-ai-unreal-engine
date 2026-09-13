// Copyright 2026, BlueprintsLab, All rights reserved.

#include "Tools/PCGTools.h"
#include "Tools/BatchToolHelper.h"
#include "Managers/EditorProfileSync.h"

#include "PCGGraph.h"
#include "PCGNode.h"
#include "PCGPin.h"
#include "PCGEdge.h"
#include "PCGSettings.h"
#include "PCGComponent.h"
#include "Elements/PCGSplineSampler.h"
#include "Elements/PCGDensityFilter.h"
#include "PCGSubsystem.h"
#include "PCGManagedResource.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/BoxComponent.h"
#include "LandscapeProxy.h"
#include "EngineUtils.h"
#include "Misc/EngineVersionComparison.h"
#if UE_VERSION_OLDER_THAN(5, 5, 0)
#include "PropertyBag.h"
#else
#include "StructUtils/PropertyBag.h"
#endif
#include "Elements/PCGTextureSampler.h"
#include "Elements/PCGDataFromActor.h"
#include "Elements/PCGAttributeFilter.h"
#include "Elements/PCGActorSelector.h"
#include "Elements/PCGSelfPruning.h"
#include "Elements/PCGCreateAttribute.h"
#include "Elements/Metadata/PCGMetadataMathsOpElement.h"
#include "Metadata/PCGMetadataTypesConstantStruct.h"
#include "Metadata/PCGAttributePropertySelector.h"
#include "PCGSubgraph.h"
// Exclusion recipe (set_pcg_exclusion): World/actor/spline exclusion sources feeding a Difference node.
#include "Elements/PCGDifferenceElement.h"
#include "Elements/PCGWorldQuery.h"
#include "Elements/PCGBoundsModifier.h"
#include "Elements/PCGStaticMeshSpawner.h"

#include "EditorAssetLibrary.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Misc/PackageName.h"
#include "UObject/Package.h"
#include "UObject/UObjectIterator.h"
#include "UObject/UnrealType.h"

#include "GameFramework/Actor.h"
#include "Engine/World.h"
#include "Engine/EngineTypes.h"
#include "EngineUtils.h"
#include "Editor.h"
#include "ScopedTransaction.h"
#include "Managers/SettingsManager.h"
#include "Misc/ConfigCacheIni.h"

#include "Serialization/JsonSerializer.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

namespace PCGTools
{

static UPCGGraph* LoadPCGGraph(const FString& GraphPath, FString& OutError)
{
	UObject* Obj = UEditorAssetLibrary::LoadAsset(GraphPath);
	UPCGGraph* Graph = Cast<UPCGGraph>(Obj);
	if (!Graph)
	{
		OutError = FString::Printf(TEXT("Could not load PCGGraph at '%s'"), *GraphPath);
	}
	return Graph;
}

static UClass* FindPCGSettingsClass(const FString& ClassName)
{
	FString Stripped = ClassName;
	if (Stripped.StartsWith(TEXT("U"))) Stripped = Stripped.Mid(1);

	UClass* Cls = FindObject<UClass>(nullptr, *(TEXT("/Script/PCG.") + Stripped));
	if (Cls && Cls->IsChildOf(UPCGSettings::StaticClass())) return Cls;

	for (TObjectIterator<UClass> It; It; ++It)
	{
		if ((It->GetName().Equals(Stripped, ESearchCase::IgnoreCase) ||
			 It->GetName().Equals(ClassName, ESearchCase::IgnoreCase)) &&
			It->IsChildOf(UPCGSettings::StaticClass()))
		{
			return *It;
		}
	}
	return nullptr;
}

static TArray<UPCGNode*> GetVirtualNodeList(UPCGGraph* Graph)
{
	TArray<UPCGNode*> All;
	All.Add(Graph->GetInputNode());
	All.Add(Graph->GetOutputNode());
	for (UPCGNode* N : Graph->GetNodes())
	{
		if (N && N != Graph->GetInputNode() && N != Graph->GetOutputNode())
			All.Add(N);
	}
	return All;
}

static UPCGNode* GetNodeByVirtualIndex(UPCGGraph* Graph, int32 VirtualIndex)
{
	if (VirtualIndex == 0) return Graph->GetInputNode();
	if (VirtualIndex == 1) return Graph->GetOutputNode();
	const TArray<UPCGNode*>& Mid = Graph->GetNodes();
	int32 MidIdx = VirtualIndex - 2;
	if (MidIdx >= 0 && MidIdx < Mid.Num()) return Mid[MidIdx];
	return nullptr;
}

static int32 GetVirtualIndexOfNode(UPCGGraph* Graph, const UPCGNode* Target)
{
	if (!Target) return -1;
	if (Target == Graph->GetInputNode())  return 0;
	if (Target == Graph->GetOutputNode()) return 1;
	const TArray<UPCGNode*>& Mid = Graph->GetNodes();
	for (int32 i = 0; i < Mid.Num(); i++)
	{
		if (Mid[i] == Target) return i + 2;
	}
	return -1;
}

static TSharedPtr<FJsonObject> BuildNodeJson(UPCGGraph* Graph, UPCGNode* Node, int32 VirtualIndex)
{
	TSharedPtr<FJsonObject> NodeObj = MakeShareable(new FJsonObject);
	NodeObj->SetNumberField(TEXT("index"), VirtualIndex);

#if WITH_EDITOR
	int32 PosX = 0, PosY = 0;
	Node->GetNodePosition(PosX, PosY);
	NodeObj->SetNumberField(TEXT("pos_x"), PosX);
	NodeObj->SetNumberField(TEXT("pos_y"), PosY);
#endif

	if (VirtualIndex == 0)      NodeObj->SetStringField(TEXT("type"), TEXT("Input"));
	else if (VirtualIndex == 1) NodeObj->SetStringField(TEXT("type"), TEXT("Output"));
	else                        NodeObj->SetStringField(TEXT("type"), TEXT("Node"));

	UPCGSettings* Settings = Node->GetSettings();
	NodeObj->SetStringField(TEXT("class"), Settings ? Settings->GetClass()->GetName() : TEXT("None"));

	if (Node->NodeTitle != NAME_None)
		NodeObj->SetStringField(TEXT("title"), Node->NodeTitle.ToString());
	if (!Node->NodeComment.IsEmpty())
		NodeObj->SetStringField(TEXT("comment"), Node->NodeComment);

	TArray<TSharedPtr<FJsonValue>> InPins;
	for (const FPCGPinProperties& Props : Node->InputPinProperties())
		InPins.Add(MakeShareable(new FJsonValueString(Props.Label.ToString())));
	NodeObj->SetArrayField(TEXT("input_pins"), InPins);

	TArray<TSharedPtr<FJsonValue>> OutPins;
	for (const FPCGPinProperties& Props : Node->OutputPinProperties())
		OutPins.Add(MakeShareable(new FJsonValueString(Props.Label.ToString())));
	NodeObj->SetArrayField(TEXT("output_pins"), OutPins);

	return NodeObj;
}

static TArray<TSharedPtr<FJsonValue>> BuildNodeArray(UPCGGraph* Graph)
{
	TArray<TSharedPtr<FJsonValue>> NodeValues;
	TArray<UPCGNode*> All = GetVirtualNodeList(Graph);
	for (int32 i = 0; i < All.Num(); i++)
	{
		if (!All[i]) continue;
		NodeValues.Add(MakeShareable(new FJsonValueObject(BuildNodeJson(Graph, All[i], i))));
	}
	return NodeValues;
}

static TArray<TSharedPtr<FJsonValue>> BuildEdgeArray(UPCGGraph* Graph)
{
	TArray<TSharedPtr<FJsonValue>> EdgeValues;
	TArray<UPCGNode*> All = GetVirtualNodeList(Graph);

	for (int32 i = 0; i < All.Num(); i++)
	{
		UPCGNode* Node = All[i];
		if (!Node) continue;

		for (const TObjectPtr<UPCGPin>& PinPtr : Node->GetOutputPins())
		{
			UPCGPin* OutPin = PinPtr.Get();
			if (!OutPin) continue;

			for (const TObjectPtr<UPCGEdge>& EdgePtr : OutPin->Edges)
			{
				UPCGEdge* Edge = EdgePtr.Get();
				if (!Edge) continue;

				UPCGPin* DestPin = Edge->OutputPin.Get();
				if (!DestPin) continue;

				UPCGNode* DestNode = DestPin->Node.Get();
				if (!DestNode) continue;

				int32 ToIdx = GetVirtualIndexOfNode(Graph, DestNode);
				if (ToIdx == -1) continue;

				TSharedPtr<FJsonObject> EdgeObj = MakeShareable(new FJsonObject);
				EdgeObj->SetNumberField(TEXT("from_node"), i);
				EdgeObj->SetStringField(TEXT("from_pin"), OutPin->Properties.Label.ToString());
				EdgeObj->SetNumberField(TEXT("to_node"), ToIdx);
				EdgeObj->SetStringField(TEXT("to_pin"), DestPin->Properties.Label.ToString());
				EdgeValues.Add(MakeShareable(new FJsonValueObject(EdgeObj)));
			}
		}
	}
	return EdgeValues;
}

static bool SetReflectionProperty(UObject* Object, const FString& PropertyName, const TSharedPtr<FJsonObject>& ValueJson, FString& OutError)
{
	if (!Object || !ValueJson.IsValid()) return false;

	UClass* Class = Object->GetClass();
	for (TFieldIterator<FProperty> PropIt(Class); PropIt; ++PropIt)
	{
		FProperty* Prop = *PropIt;
		if (!Prop->GetName().Equals(PropertyName, ESearchCase::IgnoreCase)) continue;

		void* DataPtr = Prop->ContainerPtrToValuePtr<void>(Object);

		if (FFloatProperty* FloatProp = CastField<FFloatProperty>(Prop))
		{
			double Val;
			if (ValueJson->TryGetNumberField(TEXT("float_value"), Val)) { FloatProp->SetPropertyValue(DataPtr, (float)Val); return true; }
		}
		else if (FDoubleProperty* DblProp = CastField<FDoubleProperty>(Prop))
		{
			double Val;
			if (ValueJson->TryGetNumberField(TEXT("float_value"), Val)) { DblProp->SetPropertyValue(DataPtr, Val); return true; }
		}
		else if (FIntProperty* IntProp = CastField<FIntProperty>(Prop))
		{
			double Val;
			if (ValueJson->TryGetNumberField(TEXT("int_value"), Val)) { IntProp->SetPropertyValue(DataPtr, (int32)Val); return true; }
		}
		else if (FBoolProperty* BoolProp = CastField<FBoolProperty>(Prop))
		{
			bool Val;
			if (ValueJson->TryGetBoolField(TEXT("bool_value"), Val)) { BoolProp->SetPropertyValue(DataPtr, Val); return true; }
		}
		else if (FStrProperty* StrProp = CastField<FStrProperty>(Prop))
		{
			FString Val;
			if (ValueJson->TryGetStringField(TEXT("string_value"), Val)) { StrProp->SetPropertyValue(DataPtr, Val); return true; }
		}
		else if (FNameProperty* NameProp = CastField<FNameProperty>(Prop))
		{
			FString Val;
			if (ValueJson->TryGetStringField(TEXT("string_value"), Val)) { NameProp->SetPropertyValue(DataPtr, FName(*Val)); return true; }
		}
		else if (FEnumProperty* EnumProp = CastField<FEnumProperty>(Prop))
		{
			FString Val;
			if (ValueJson->TryGetStringField(TEXT("string_value"), Val))
			{
				UEnum* Enum = EnumProp->GetEnum();
				int64 EnumVal = Enum->GetValueByNameString(Val);
				if (EnumVal != INDEX_NONE) { EnumProp->GetUnderlyingProperty()->SetIntPropertyValue(DataPtr, EnumVal); return true; }
				OutError = FString::Printf(TEXT("Enum value '%s' not found in %s"), *Val, *Enum->GetName());
				return false;
			}
		}
		else if (FByteProperty* ByteProp = CastField<FByteProperty>(Prop))
		{
			if (ByteProp->Enum)
			{
				FString Val;
				if (ValueJson->TryGetStringField(TEXT("string_value"), Val))
				{
					int64 EnumVal = ByteProp->Enum->GetValueByNameString(Val);
					if (EnumVal != INDEX_NONE) { ByteProp->SetPropertyValue(DataPtr, (uint8)EnumVal); return true; }
				}
			}
			else
			{
				double Val;
				if (ValueJson->TryGetNumberField(TEXT("int_value"), Val)) { ByteProp->SetPropertyValue(DataPtr, (uint8)Val); return true; }
			}
		}
		else if (FObjectProperty* ObjProp = CastField<FObjectProperty>(Prop))
		{
			FString Val;
			if (ValueJson->TryGetStringField(TEXT("string_value"), Val))
			{
				UObject* Loaded = UEditorAssetLibrary::LoadAsset(Val);
				if (Loaded && Loaded->IsA(ObjProp->PropertyClass)) { ObjProp->SetObjectPropertyValue(DataPtr, Loaded); return true; }
				OutError = FString::Printf(TEXT("Could not load asset '%s' for property '%s'"), *Val, *PropertyName);
				return false;
			}
		}
		else if (FStructProperty* StructProp = CastField<FStructProperty>(Prop))
		{
			FString Val;
			if (ValueJson->TryGetStringField(TEXT("string_value"), Val))
			{
				if (StructProp->Struct == TBaseStructure<FVector>::Get())
				{
					TArray<FString> Parts;
					Val.ParseIntoArray(Parts, TEXT(","));
					FVector Vec(0.f);
					if (Parts.Num() == 3)
					{
						Vec.X = FCString::Atof(*Parts[0].TrimStartAndEnd());
						Vec.Y = FCString::Atof(*Parts[1].TrimStartAndEnd());
						Vec.Z = FCString::Atof(*Parts[2].TrimStartAndEnd());
					}
					else
					{
						float Uniform = FCString::Atof(*Val.TrimStartAndEnd());
						Vec = FVector(Uniform);
					}
					*StructProp->ContainerPtrToValuePtr<FVector>(Object) = Vec;
					return true;
				}
				else if (StructProp->Struct == TBaseStructure<FRotator>::Get())
				{
					TArray<FString> Parts;
					Val.ParseIntoArray(Parts, TEXT(","));
					FRotator Rot(0.f, 0.f, 0.f);
					if (Parts.Num() == 3)
					{
						Rot.Pitch = FCString::Atof(*Parts[0].TrimStartAndEnd());
						Rot.Yaw   = FCString::Atof(*Parts[1].TrimStartAndEnd());
						Rot.Roll  = FCString::Atof(*Parts[2].TrimStartAndEnd());
					}
					*StructProp->ContainerPtrToValuePtr<FRotator>(Object) = Rot;
					return true;
				}
				else if (StructProp->Struct == TBaseStructure<FVector2D>::Get())
				{
					TArray<FString> Parts;
					Val.ParseIntoArray(Parts, TEXT(","));
					FVector2D Vec2(0.f, 0.f);
					if (Parts.Num() == 2)
					{
						Vec2.X = FCString::Atof(*Parts[0].TrimStartAndEnd());
						Vec2.Y = FCString::Atof(*Parts[1].TrimStartAndEnd());
					}
					*StructProp->ContainerPtrToValuePtr<FVector2D>(Object) = Vec2;
					return true;
				}

				OutError = FString::Printf(TEXT("Property '%s' is a struct (%s). Use string_value with comma-separated values: FVector='X,Y,Z', FRotator='Pitch,Yaw,Roll', FVector2D='X,Y'"), *PropertyName, *StructProp->Struct->GetName());
				return false;
			}
		}

		OutError = FString::Printf(TEXT("Property '%s' found (type: %s) but no matching value key. Use: float_value, int_value, bool_value, string_value. For FVector use string_value='X,Y,Z'. For FRotator use string_value='Pitch,Yaw,Roll'."), *PropertyName, *Prop->GetClass()->GetName());
		return false;
	}

	OutError = FString::Printf(TEXT("Property '%s' not found on %s"), *PropertyName, *Object->GetClass()->GetName());
	return false;
}

// ---------------------------------------------------------------------------------------------
// Shared level/actor summary helpers — used by get_pcg_level_summary, the generate_pcg budget
// guard, and the spawn_pcg_actor / set_pcg_actor_bounds overlap warnings.
// ---------------------------------------------------------------------------------------------

static int32 CountPCGInstances(UPCGComponent* PCGComp, int32& OutISMComponents)
{
	OutISMComponents = 0;
	int32 TotalInstances = 0;
	if (!PCGComp) return 0;

	PCGComp->ForEachManagedResource([&](UPCGManagedResource* Resource)
	{
		UPCGManagedComponent* ManagedComp = Cast<UPCGManagedComponent>(Resource);
		if (!ManagedComp) return;
		UActorComponent* ActorComp = ManagedComp->GeneratedComponent.Get();
		if (UInstancedStaticMeshComponent* ISMComp = Cast<UInstancedStaticMeshComponent>(ActorComp))
		{
			OutISMComponents++;
			TotalInstances += ISMComp->GetInstanceCount();
		}
	});
	return TotalInstances;
}

static FString PCGGenerationTriggerToString(EPCGComponentGenerationTrigger Trigger)
{
	switch (Trigger)
	{
		case EPCGComponentGenerationTrigger::GenerateOnLoad:    return TEXT("GenerateOnLoad");
		case EPCGComponentGenerationTrigger::GenerateOnDemand:  return TEXT("GenerateOnDemand");
		case EPCGComponentGenerationTrigger::GenerateAtRuntime: return TEXT("GenerateAtRuntime");
		default: return TEXT("Unknown");
	}
}

static void GetPCGActorWorldAABB(AActor* Actor, FVector& OutCenter, FVector& OutExtent)
{
	OutCenter = FVector::ZeroVector;
	OutExtent = FVector::ZeroVector;
	if (!Actor) return;

	if (UBoxComponent* BoxComp = Actor->FindComponentByClass<UBoxComponent>())
	{
		OutCenter = BoxComp->GetComponentLocation();
		OutExtent = BoxComp->GetScaledBoxExtent();
		return;
	}

	FVector Origin, Extent;
	Actor->GetActorBounds(false, Origin, Extent);
	OutCenter = Origin;
	OutExtent = Extent;
}

// Fraction of the SMALLER box's XY area covered by the AABB intersection. 0 = no overlap.
static float ComputePCGBoxOverlapFractionXY(const FVector& CenterA, const FVector& ExtentA, const FVector& CenterB, const FVector& ExtentB)
{
	const float AMinX = CenterA.X - ExtentA.X, AMaxX = CenterA.X + ExtentA.X;
	const float AMinY = CenterA.Y - ExtentA.Y, AMaxY = CenterA.Y + ExtentA.Y;
	const float BMinX = CenterB.X - ExtentB.X, BMaxX = CenterB.X + ExtentB.X;
	const float BMinY = CenterB.Y - ExtentB.Y, BMaxY = CenterB.Y + ExtentB.Y;

	const float OverlapX = FMath::Max(0.f, FMath::Min(AMaxX, BMaxX) - FMath::Max(AMinX, BMinX));
	const float OverlapY = FMath::Max(0.f, FMath::Min(AMaxY, BMaxY) - FMath::Max(AMinY, BMinY));
	const float OverlapArea = OverlapX * OverlapY;
	if (OverlapArea <= 0.f) return 0.f;

	const float AreaA = (AMaxX - AMinX) * (AMaxY - AMinY);
	const float AreaB = (BMaxX - BMinX) * (BMaxY - BMinY);
	const float SmallerArea = FMath::Min(AreaA, AreaB);
	return SmallerArea > 0.f ? (OverlapArea / SmallerArea) : 0.f;
}

struct FPCGActorSummary
{
	AActor* Actor = nullptr;
	UPCGComponent* PCGComp = nullptr;
	FString Label;
	FString GraphPath;
	FVector Center = FVector::ZeroVector;
	FVector Extent = FVector::ZeroVector;
	FString GenerationTrigger;
	bool bGenerated = false;
	bool bActivated = false;
	int32 ISMComponents = 0;
	int32 TotalInstances = 0;
};

static TArray<FPCGActorSummary> GatherPCGActorSummaries(UWorld* World)
{
	TArray<FPCGActorSummary> Out;
	if (!World) return Out;

	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Actor = *It;
		if (!Actor) continue;
		UPCGComponent* PCGComp = Actor->FindComponentByClass<UPCGComponent>();
		if (!PCGComp) continue;

		FPCGActorSummary S;
		S.Actor = Actor;
		S.PCGComp = PCGComp;
		S.Label = Actor->GetActorLabel();
		UPCGGraph* Graph = PCGComp->GetGraph();
		S.GraphPath = Graph ? Graph->GetPathName() : TEXT("None");
		GetPCGActorWorldAABB(Actor, S.Center, S.Extent);
		S.GenerationTrigger = PCGGenerationTriggerToString(PCGComp->GenerationTrigger);
		S.bGenerated = PCGComp->bGenerated;
		S.bActivated = PCGComp->IsActive();
		S.TotalInstances = CountPCGInstances(PCGComp, S.ISMComponents);
		Out.Add(S);
	}
	return Out;
}

// Default 60000, overridable via [BpGeneratorUltimate] PCGInstanceBudget=<int> in the global config.
static int32 GetPCGInstanceBudget()
{
	int32 Budget = 60000;
	GConfig->GetInt(TEXT("BpGeneratorUltimate"), TEXT("PCGInstanceBudget"), Budget, FSettingsManager::GetGlobalConfigPath());
	return Budget > 0 ? Budget : 60000;
}

// "PG_Forest_Autumn" -> "PG" ; used to flag near-duplicate/stacked graphs sharing a naming family.
static FString GetGraphNamePrefix(const FString& GraphPath)
{
	FString Name = FPackageName::GetShortName(GraphPath);
	int32 UnderscoreIdx = INDEX_NONE;
	if (Name.FindChar(TEXT('_'), UnderscoreIdx) && UnderscoreIdx > 0)
		return Name.Left(UnderscoreIdx);
	return Name;
}

// Appends "overlaps"/"warning" fields to Out for a just-spawned/resized PCG actor, comparing it
// against every other PCG actor in the level. Flags overlaps >= 50% that share a graph or a
// graph-name family as likely duplicate/stacked layers (the "4x overlapping forests" bug).
static void AppendPCGActorOverlapWarning(const TSharedPtr<FJsonObject>& Out, AActor* TargetActor, const FString& GraphPath, UWorld* World)
{
	if (!Out.IsValid() || !TargetActor || !World) return;

	FVector Center, Extent;
	GetPCGActorWorldAABB(TargetActor, Center, Extent);
	const FString Prefix = GetGraphNamePrefix(GraphPath);

	TArray<TSharedPtr<FJsonValue>> Overlaps;
	FString WarningMsg;

	for (const FPCGActorSummary& Other : GatherPCGActorSummaries(World))
	{
		if (Other.Actor == TargetActor) continue;
		const float Fraction = ComputePCGBoxOverlapFractionXY(Center, Extent, Other.Center, Other.Extent);
		if (Fraction < 0.5f) continue;

		const bool bSameGraph  = Other.GraphPath.Equals(GraphPath, ESearchCase::IgnoreCase);
		const bool bSameFamily = !Prefix.IsEmpty() && GetGraphNamePrefix(Other.GraphPath).Equals(Prefix, ESearchCase::IgnoreCase);

		TSharedPtr<FJsonObject> O = MakeShareable(new FJsonObject);
		O->SetStringField(TEXT("actor_label"), Other.Label);
		O->SetStringField(TEXT("graph_path"), Other.GraphPath);
		O->SetNumberField(TEXT("overlap_fraction"), Fraction);
		O->SetBoolField(TEXT("same_graph_family"), bSameGraph || bSameFamily);
		Overlaps.Add(MakeShareable(new FJsonValueObject(O)));

		if ((bSameGraph || bSameFamily) && WarningMsg.IsEmpty())
		{
			WarningMsg = FString::Printf(
				TEXT("Overlaps %.0f%% with existing PCG actor '%s' using a related graph ('%s') — likely a duplicate/stacked layer. ")
				TEXT("Consider deleting one, or excluding it via set_pcg_exclusion instead of stacking full-map layers."),
				Fraction * 100.f, *Other.Label, *Other.GraphPath);
		}
	}

	if (Overlaps.Num() > 0) Out->SetArrayField(TEXT("overlaps"), Overlaps);
	if (!WarningMsg.IsEmpty()) Out->SetStringField(TEXT("warning"), WarningMsg);
}

// ---------------------------------------------------------------------------------------------
// set_pcg_exclusion node markers — stashed in UPCGNode::NodeComment so repeated calls can find
// and reuse the exclusion-source nodes / Difference node they previously created (idempotency).
// ---------------------------------------------------------------------------------------------
namespace PCGExclusionMarkers
{
	static const FName InPin(TEXT("In"));
	static const FName OutPin(TEXT("Out"));

	static FString World()                        { return TEXT("[AxivorExclusion:world]"); }
	static FString Tag(const FString& T)          { return FString::Printf(TEXT("[AxivorExclusion:tag:%s]"), *T); }
	static FString Class(const FString& C)        { return FString::Printf(TEXT("[AxivorExclusion:class:%s]"), *C); }
	static FString Spline(const FString& T)       { return FString::Printf(TEXT("[AxivorExclusion:spline:%s]"), *T); }
	static const TCHAR* Diff()                    { return TEXT("[AxivorExclusion:diff]"); }

	static UPCGNode* FindByMarker(UPCGGraph* Graph, const FString& Marker)
	{
		for (UPCGNode* Node : Graph->GetNodes())
		{
			if (Node && Node->NodeComment.Contains(Marker)) return Node;
		}
		return nullptr;
	}

	static bool IsExclusionDifference(UPCGNode* Node)
	{
		if (!Node) return false;
		if (!Cast<UPCGDifferenceSettings>(Node->GetSettings())) return false;
		return Node->NodeComment.Contains(Diff());
	}
}

void HandleCreatePCGGraph(const FString& Name, const FString& SavePath, FString& OutJsonString, FString& OutError)
{

	if (Name.IsEmpty()) { OutError = TEXT("'name' is required"); return; }

	FString PackagePath = SavePath.IsEmpty() ? TEXT("/Game/PCG") : SavePath;
	if (!PackagePath.StartsWith(TEXT("/"))) PackagePath = TEXT("/Game/") + PackagePath;

	FString FullPackageName = PackagePath / Name;

	if (UEditorAssetLibrary::DoesAssetExist(FullPackageName))
	{
		TSharedPtr<FJsonObject> Out = MakeShareable(new FJsonObject);
		Out->SetBoolField(TEXT("success"), true);
		Out->SetStringField(TEXT("graph_path"), FullPackageName);
		Out->SetStringField(TEXT("message"), TEXT("PCGGraph already exists at this path."));
		TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&OutJsonString);
		FJsonSerializer::Serialize(Out.ToSharedRef(), W);
		return;
	}

	UPackage* Package = CreatePackage(*FullPackageName);
	if (!Package) { OutError = FString::Printf(TEXT("Failed to create package '%s'"), *FullPackageName); return; }
	Package->FullyLoad();

	UPCGGraph* Graph = NewObject<UPCGGraph>(Package, *Name, RF_Public | RF_Standalone | RF_Transactional);
	if (!Graph) { OutError = TEXT("NewObject<UPCGGraph> returned null"); return; }

	Graph->PostInitProperties();

	FAssetRegistryModule::AssetCreated(Graph);
	Graph->MarkPackageDirty();

	TSharedPtr<FJsonObject> Out = MakeShareable(new FJsonObject);
	Out->SetBoolField(TEXT("success"), true);
	Out->SetStringField(TEXT("graph_path"), FullPackageName);
	Out->SetStringField(TEXT("message"), FString::Printf(TEXT("PCGGraph '%s' created. Input=node[0], Output=node[1]. Use add_pcg_node then connect_pcg_nodes."), *FullPackageName));

	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutJsonString);
	FJsonSerializer::Serialize(Out.ToSharedRef(), Writer);
}

void HandleGetPCGGraphSummary(const FString& GraphPath, FString& OutJsonString, FString& OutError)
{

	UPCGGraph* Graph = LoadPCGGraph(GraphPath, OutError);
	if (!Graph) return;

	TSharedPtr<FJsonObject> Out = MakeShareable(new FJsonObject);
	Out->SetBoolField(TEXT("success"), true);
	Out->SetStringField(TEXT("graph_path"), GraphPath);
	Out->SetArrayField(TEXT("nodes"), BuildNodeArray(Graph));
	Out->SetArrayField(TEXT("edges"), BuildEdgeArray(Graph));
	Out->SetStringField(TEXT("note"), TEXT("type=Input is node[0], type=Output is node[1]. Use index values in connect_pcg_nodes and set_pcg_node_property."));

	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutJsonString);
	FJsonSerializer::Serialize(Out.ToSharedRef(), Writer);
}

void HandleAddPCGNode(const FString& GraphPath, const FString& SettingsClass, int32 PosX, int32 PosY, FString& OutJsonString, FString& OutError)
{

	UPCGGraph* Graph = LoadPCGGraph(GraphPath, OutError);
	if (!Graph) return;

	UClass* Cls = FindPCGSettingsClass(SettingsClass);
	if (!Cls)
	{
		TArray<FString> Available;
		for (TObjectIterator<UClass> It; It; ++It)
		{
			if (It->IsChildOf(UPCGSettings::StaticClass()) && !It->HasAnyClassFlags(CLASS_Abstract))
			{
				Available.Add(It->GetName());
			}
		}
		Available.Sort();
		OutError = FString::Printf(
			TEXT("PCG settings class '%s' not found. All available PCG settings classes: %s"),
			*SettingsClass, *FString::Join(Available, TEXT(", ")));
		return;
	}

	UPCGSettings* DefaultSettings = nullptr;
	UPCGNode* NewNode = Graph->AddNodeOfType(Cls, DefaultSettings);
	if (!NewNode) { OutError = TEXT("Graph->AddNodeOfType returned null"); return; }

	if (SettingsClass.Contains(TEXT("GetLandscape")))
	{
		UPCGDataFromActorSettings* LandscapeSettings = Cast<UPCGDataFromActorSettings>(DefaultSettings);
		if (LandscapeSettings)
		{
			LandscapeSettings->ActorSelector.bMustOverlapSelf = false;
#if WITH_EDITORONLY_DATA
			LandscapeSettings->bTrackActorsOnlyWithinBounds = false;
#endif
		}
	}

#if WITH_EDITOR
	NewNode->SetNodePosition(PosX, PosY);
#endif

	Graph->MarkPackageDirty();
	if (UWorld* PCGWorld = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr)
	{
		if (UPCGSubsystem* Sub = PCGWorld->GetSubsystem<UPCGSubsystem>())
			Sub->NotifyGraphChanged(Graph, EPCGChangeType::Structural);
	}

	int32 NewIndex = GetVirtualIndexOfNode(Graph, NewNode);

	TSharedPtr<FJsonObject> Out = MakeShareable(new FJsonObject);
	Out->SetBoolField(TEXT("success"), true);
	Out->SetNumberField(TEXT("node_index"), NewIndex);
	Out->SetStringField(TEXT("class"), Cls->GetName());
	Out->SetStringField(TEXT("message"), FString::Printf(TEXT("Added '%s' node at index %d. Wire it with connect_pcg_nodes, configure with set_pcg_node_property."), *Cls->GetName(), NewIndex));

	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutJsonString);
	FJsonSerializer::Serialize(Out.ToSharedRef(), Writer);
}

void HandleRemovePCGNode(const FString& GraphPath, int32 NodeIndex, FString& OutJsonString, FString& OutError)
{

	UPCGGraph* Graph = LoadPCGGraph(GraphPath, OutError);
	if (!Graph) return;

	if (NodeIndex == 0 || NodeIndex == 1)
	{
		OutError = TEXT("Cannot remove the built-in Input (0) or Output (1) node");
		return;
	}

	TArray<UPCGNode*> All = GetVirtualNodeList(Graph);
	if (NodeIndex < 0 || NodeIndex >= All.Num())
	{
		OutError = FString::Printf(TEXT("Node index %d is out of range (graph has %d nodes)"), NodeIndex, All.Num());
		return;
	}

	UPCGNode* NodeToRemove = All[NodeIndex];

	Graph->RemoveNode(NodeToRemove);
	Graph->MarkPackageDirty();
	if (UWorld* PCGWorld = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr)
	{
		if (UPCGSubsystem* Sub = PCGWorld->GetSubsystem<UPCGSubsystem>())
			Sub->NotifyGraphChanged(Graph, EPCGChangeType::Structural);
	}

	TSharedPtr<FJsonObject> Out = MakeShareable(new FJsonObject);
	Out->SetBoolField(TEXT("success"), true);
	Out->SetStringField(TEXT("message"), FString::Printf(TEXT("Removed node at index %d. Call get_pcg_graph_summary to get updated indices."), NodeIndex));

	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutJsonString);
	FJsonSerializer::Serialize(Out.ToSharedRef(), Writer);
}

void HandleConnectPCGNodes(const FString& GraphPath, int32 FromNodeIndex, int32 ToNodeIndex, const FString& FromPin, const FString& ToPin, FString& OutJsonString, FString& OutError)
{

	UPCGGraph* Graph = LoadPCGGraph(GraphPath, OutError);
	if (!Graph) return;

	TArray<UPCGNode*> All = GetVirtualNodeList(Graph);

	if (FromNodeIndex < 0 || FromNodeIndex >= All.Num())
	{
		OutError = FString::Printf(TEXT("from_node_index %d out of range (%d nodes)"), FromNodeIndex, All.Num());
		return;
	}
	if (ToNodeIndex < 0 || ToNodeIndex >= All.Num())
	{
		OutError = FString::Printf(TEXT("to_node_index %d out of range (%d nodes)"), ToNodeIndex, All.Num());
		return;
	}

	UPCGNode* FromNode = All[FromNodeIndex];
	UPCGNode* ToNode   = All[ToNodeIndex];

	FName FromPinLabel = FromPin.IsEmpty() ? FName(TEXT("Out")) : FName(*FromPin);
	FName ToPinLabel   = ToPin.IsEmpty()   ? FName(TEXT("In"))  : FName(*ToPin);

	UPCGPin* OutPin = FromNode->GetOutputPin(FromPinLabel);
	UPCGPin* InPin  = ToNode->GetInputPin(ToPinLabel);
	if (!OutPin)
	{
		OutError = FString::Printf(TEXT("from_pin '%s' not found on node[%d]. Use get_pcg_graph_summary to see available pins."), *FromPinLabel.ToString(), FromNodeIndex);
		return;
	}
	if (!InPin)
	{
		OutError = FString::Printf(TEXT("to_pin '%s' not found on node[%d]. Use get_pcg_graph_summary to see available pins."), *ToPinLabel.ToString(), ToNodeIndex);
		return;
	}
	bool bEdgeAdded = OutPin->AddEdgeTo(InPin) != 0;
	if (!bEdgeAdded)
	{
		OutError = FString::Printf(
			TEXT("AddEdgeTo failed: %s.%s -> %s.%s — pins may be incompatible types."),
			*FromPinLabel.ToString(), *FString::FromInt(FromNodeIndex), *ToPinLabel.ToString(), *FString::FromInt(ToNodeIndex));
		return;
	}

	Graph->MarkPackageDirty();
	if (UWorld* PCGWorld = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr)
	{
		if (UPCGSubsystem* Sub = PCGWorld->GetSubsystem<UPCGSubsystem>())
			Sub->NotifyGraphChanged(Graph, EPCGChangeType::Structural);
	}

	TSharedPtr<FJsonObject> Out = MakeShareable(new FJsonObject);
	Out->SetBoolField(TEXT("success"), true);
	Out->SetStringField(TEXT("message"), FString::Printf(TEXT("Connected node[%d].%s -> node[%d].%s"), FromNodeIndex, *FromPinLabel.ToString(), ToNodeIndex, *ToPinLabel.ToString()));

	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutJsonString);
	FJsonSerializer::Serialize(Out.ToSharedRef(), Writer);
}

void HandleSetPCGNodeProperty(const FString& GraphPath, int32 NodeIndex, const FString& PropertyName, const TSharedPtr<FJsonObject>& ValueJson, FString& OutJsonString, FString& OutError)
{

	UPCGGraph* Graph = LoadPCGGraph(GraphPath, OutError);
	if (!Graph) return;

	TArray<UPCGNode*> All = GetVirtualNodeList(Graph);
	if (NodeIndex < 0 || NodeIndex >= All.Num())
	{
		OutError = FString::Printf(TEXT("Node index %d out of range (%d nodes)"), NodeIndex, All.Num());
		return;
	}

	UPCGNode* Node = All[NodeIndex];
	UPCGSettings* Settings = Node ? Node->GetSettings() : nullptr;
	if (!Settings)
	{
		OutError = FString::Printf(TEXT("Node %d has no settings object"), NodeIndex);
		return;
	}

	if (!SetReflectionProperty(Settings, PropertyName, ValueJson, OutError))
	{
		if (OutError.IsEmpty())
		{
			TArray<FString> PropNames;
			for (TFieldIterator<FProperty> It(Settings->GetClass()); It; ++It)
			{
				PropNames.Add(It->GetName());
			}
			OutError = FString::Printf(TEXT("Property '%s' not found. Available on %s: %s"),
				*PropertyName, *Settings->GetClass()->GetName(), *FString::Join(PropNames, TEXT(", ")));
		}
		return;
	}

	Settings->MarkPackageDirty();
	Graph->MarkPackageDirty();
	if (UWorld* PropWorld = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr)
	{
		if (UPCGSubsystem* PCGSub = PropWorld->GetSubsystem<UPCGSubsystem>())
		{
			PCGSub->NotifyGraphChanged(Graph, EPCGChangeType::Settings);
		}
	}

	TSharedPtr<FJsonObject> Out = MakeShareable(new FJsonObject);
	Out->SetBoolField(TEXT("success"), true);
	Out->SetStringField(TEXT("message"), FString::Printf(TEXT("Set '%s' on node[%d] (%s)"), *PropertyName, NodeIndex, *Settings->GetClass()->GetName()));

	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutJsonString);
	FJsonSerializer::Serialize(Out.ToSharedRef(), Writer);
}

void HandleSpawnPCGActor(const FString& GraphPath, const FString& ActorLabel,
	float LocationX, float LocationY, float LocationZ,
	float BoundsX, float BoundsY, float BoundsZ,
	FString& OutJsonString, FString& OutError)
{

	UObject* Obj = UEditorAssetLibrary::LoadAsset(GraphPath);
	UPCGGraph* Graph = Cast<UPCGGraph>(Obj);
	if (!Graph) { OutError = FString::Printf(TEXT("Could not load PCGGraph at '%s'"), *GraphPath); return; }

	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!World) { OutError = TEXT("No editor world available"); return; }

	// Caller-provided bounds/location win. Only fall back to auto-detecting the landscape's bounds
	// when the caller didn't give explicit bounds — previously this always overrode both,
	// which is how "add biomes/grass/roads" turned into 4 actors all sized to the whole map.
	const bool bCallerGaveBounds = BoundsX > 0.f && BoundsY > 0.f && BoundsZ > 0.f;
	const bool bCallerGaveLocation = !FMath::IsNearlyZero(LocationX) || !FMath::IsNearlyZero(LocationY) || !FMath::IsNearlyZero(LocationZ);

	float HX = BoundsX > 0.f ? BoundsX : 0.f;
	float HY = BoundsY > 0.f ? BoundsY : 0.f;
	float HZ = BoundsZ > 0.f ? BoundsZ : 0.f;
	FVector Location(LocationX, LocationY, LocationZ);
	bool bBoundsAutoDetected = false;

	if (!bCallerGaveBounds)
	{
		for (TActorIterator<ALandscapeProxy> It(World); It; ++It)
		{
			ALandscapeProxy* Landscape = *It;
			if (!Landscape) continue;

			FVector LandOrigin, LandExtent;
			Landscape->GetActorBounds(false, LandOrigin, LandExtent);

			if (!bCallerGaveLocation) Location = LandOrigin;
			HX = LandExtent.X > 0.f ? LandExtent.X : 50000.f;
			HY = LandExtent.Y > 0.f ? LandExtent.Y : 50000.f;
			HZ = LandExtent.Z > 0.f ? LandExtent.Z * 2.f : 10000.f;
			bBoundsAutoDetected = true;

			UE_LOG(LogTemp, Log, TEXT("PCG: Auto-detected landscape '%s' bounds — center=(%.0f,%.0f,%.0f) extent=(%.0f,%.0f,%.0f)"),
				*Landscape->GetActorLabel(), Location.X, Location.Y, Location.Z, HX, HY, HZ);
			break;
		}

		if (HX <= 0.f) HX = 50000.f;
		if (HY <= 0.f) HY = 50000.f;
		if (HZ <= 0.f) HZ = 10000.f;
	}
	FActorSpawnParameters SpawnParams;

	AActor* NewActor = World->SpawnActor<AActor>(AActor::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, SpawnParams);
	if (!NewActor) { OutError = TEXT("Failed to spawn actor in level"); return; }
	NewActor->SetActorLabel(ActorLabel);

	UBoxComponent* BoxComp = NewObject<UBoxComponent>(NewActor, TEXT("PCGVolume"));
	NewActor->SetRootComponent(BoxComp);
	BoxComp->RegisterComponent();
	BoxComp->SetBoxExtent(FVector(HX, HY, HZ));
	BoxComp->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	NewActor->SetActorLocation(Location);
	UE_LOG(LogTemp, Log, TEXT("PCG: Spawned actor '%s' at (%.0f,%.0f,%.0f) with bounds (%.0f,%.0f,%.0f)"),
		*ActorLabel, Location.X, Location.Y, Location.Z, HX, HY, HZ);

	UPCGComponent* PCGComp = NewObject<UPCGComponent>(NewActor, TEXT("PCGComponent"));
	NewActor->AddInstanceComponent(PCGComp);
	PCGComp->RegisterComponent();
	PCGComp->SetGraph(Graph);
	PCGComp->Activate(true);

	UE_LOG(LogTemp, Log, TEXT("PCG: Component registered and activated, graph=%s"),
		Graph ? *Graph->GetName() : TEXT("null"));

	World->MarkPackageDirty();
	if (GEditor) GEditor->RedrawAllViewports();

	TSharedPtr<FJsonObject> Out = MakeShareable(new FJsonObject);
	Out->SetBoolField(TEXT("success"), true);
	Out->SetStringField(TEXT("actor_label"), NewActor->GetActorLabel());
	Out->SetNumberField(TEXT("bounds_x"), HX);
	Out->SetNumberField(TEXT("bounds_y"), HY);
	Out->SetNumberField(TEXT("bounds_z"), HZ);
	if (bBoundsAutoDetected) Out->SetBoolField(TEXT("bounds_auto_detected_from_landscape"), true);
	AppendPCGActorOverlapWarning(Out, NewActor, GraphPath, World);
	Out->SetStringField(TEXT("message"), FString::Printf(
		TEXT("Spawned PCG actor '%s' at (%.1f,%.1f,%.1f) with bounds %.0fx%.0fx%.0fcm (half-extents). Use generate_pcg to run generation. For landscape scatter, bounds should match landscape half-size."),
		*NewActor->GetActorLabel(), Location.X, Location.Y, Location.Z, HX*2.f, HY*2.f, HZ*2.f));

	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutJsonString);
	FJsonSerializer::Serialize(Out.ToSharedRef(), Writer);
}

void HandleSetPCGActorBounds(const FString& ActorLabel, float BoundsX, float BoundsY, float BoundsZ, FString& OutJsonString, FString& OutError)
{

	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!World) { OutError = TEXT("No editor world available"); return; }

	AActor* TargetActor = nullptr;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		if (It->GetActorLabel().Equals(ActorLabel, ESearchCase::IgnoreCase)) { TargetActor = *It; break; }
	}
	if (!TargetActor) { OutError = FString::Printf(TEXT("No actor with label '%s' found"), *ActorLabel); return; }

	UBoxComponent* BoxComp = TargetActor->FindComponentByClass<UBoxComponent>();
	if (!BoxComp) { OutError = FString::Printf(TEXT("Actor '%s' has no BoxComponent to resize"), *ActorLabel); return; }

	BoxComp->SetBoxExtent(FVector(BoundsX, BoundsY, BoundsZ));
	World->MarkPackageDirty();
	if (GEditor) GEditor->RedrawAllViewports();

	UPCGComponent* PCGComp = TargetActor->FindComponentByClass<UPCGComponent>();
	if (PCGComp) PCGComp->GenerateLocal(true);

	FString CurrentGraphPath;
	if (PCGComp) { if (UPCGGraph* CurGraph = PCGComp->GetGraph()) CurrentGraphPath = CurGraph->GetPathName(); }

	TSharedPtr<FJsonObject> Out = MakeShareable(new FJsonObject);
	Out->SetBoolField(TEXT("success"), true);
	Out->SetStringField(TEXT("actor_label"), ActorLabel);
	Out->SetNumberField(TEXT("bounds_x"), BoundsX);
	Out->SetNumberField(TEXT("bounds_y"), BoundsY);
	Out->SetNumberField(TEXT("bounds_z"), BoundsZ);
	AppendPCGActorOverlapWarning(Out, TargetActor, CurrentGraphPath, World);
	Out->SetStringField(TEXT("message"), FString::Printf(
		TEXT("Resized PCG actor '%s' bounds to %.0fx%.0fx%.0fcm (half-extents = %.0fx%.0fx%.0fm world size). PCG regenerated."),
		*ActorLabel, BoundsX, BoundsY, BoundsZ, BoundsX*2.f/100.f, BoundsY*2.f/100.f, BoundsZ*2.f/100.f));

	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutJsonString);
	FJsonSerializer::Serialize(Out.ToSharedRef(), Writer);
}

void HandleGetPCGActorInfo(const FString& ActorLabel, FString& OutJsonString, FString& OutError)
{

	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!World) { OutError = TEXT("No editor world available"); return; }

	AActor* TargetActor = nullptr;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		if (It->GetActorLabel().Equals(ActorLabel, ESearchCase::IgnoreCase)) { TargetActor = *It; break; }
	}
	if (!TargetActor) { OutError = FString::Printf(TEXT("No actor with label '%s' found in the current level"), *ActorLabel); return; }

	TSharedPtr<FJsonObject> Out = MakeShareable(new FJsonObject);
	Out->SetBoolField(TEXT("success"), true);
	Out->SetStringField(TEXT("actor_label"), TargetActor->GetActorLabel());

	FVector Loc = TargetActor->GetActorLocation();
	Out->SetNumberField(TEXT("location_x"), Loc.X);
	Out->SetNumberField(TEXT("location_y"), Loc.Y);
	Out->SetNumberField(TEXT("location_z"), Loc.Z);

	UBoxComponent* BoxComp = TargetActor->FindComponentByClass<UBoxComponent>();
	if (BoxComp)
	{
		FVector Ext = BoxComp->GetScaledBoxExtent();
		Out->SetNumberField(TEXT("bounds_half_x"), Ext.X);
		Out->SetNumberField(TEXT("bounds_half_y"), Ext.Y);
		Out->SetNumberField(TEXT("bounds_half_z"), Ext.Z);
		Out->SetNumberField(TEXT("world_size_x_m"), Ext.X * 2.f / 100.f);
		Out->SetNumberField(TEXT("world_size_y_m"), Ext.Y * 2.f / 100.f);
	}
	else
	{
		Out->SetStringField(TEXT("bounds"), TEXT("No BoxComponent found"));
	}

	UPCGComponent* PCGComp = TargetActor->FindComponentByClass<UPCGComponent>();
	if (PCGComp)
	{
		Out->SetBoolField(TEXT("pcg_component_found"), true);
		UPCGGraph* Graph = PCGComp->GetGraph();
		Out->SetStringField(TEXT("graph_path"), Graph ? Graph->GetPathName() : TEXT("None"));
		Out->SetBoolField(TEXT("is_active"), PCGComp->IsActive());
	}
	else
	{
		Out->SetBoolField(TEXT("pcg_component_found"), false);
		Out->SetStringField(TEXT("error_hint"), TEXT("Actor has no PCGComponent — use spawn_pcg_actor or update_pcg_actor_graph to attach one"));
	}

	Out->SetStringField(TEXT("tip"), TEXT("If bounds are too small for your landscape, use set_pcg_actor_bounds to resize then generate_pcg will auto-run."));

	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutJsonString);
	FJsonSerializer::Serialize(Out.ToSharedRef(), Writer);
}

void HandleSetPCGTransformRandomizer(const FString& GraphPath, int32 NodeIndex,
	float ScaleMin, float ScaleMax, bool bUniformScale,
	float RotMinZ, float RotMaxZ,
	float OffsetMinZ, float OffsetMaxZ,
	float RotMinX, float RotMaxX, float RotMinY, float RotMaxY,
	bool bAbsoluteRotation,
	FString& OutJsonString, FString& OutError)
{

	UPCGGraph* Graph = LoadPCGGraph(GraphPath, OutError);
	if (!Graph) return;

	TArray<UPCGNode*> All = GetVirtualNodeList(Graph);
	if (NodeIndex < 0 || NodeIndex >= All.Num())
	{
		OutError = FString::Printf(TEXT("Node index %d out of range (%d nodes)"), NodeIndex, All.Num());
		return;
	}

	UPCGNode* Node = All[NodeIndex];
	UPCGSettings* Settings = Node ? Node->GetSettings() : nullptr;
	if (!Settings)
	{
		OutError = FString::Printf(TEXT("Node %d has no settings"), NodeIndex);
		return;
	}

	if (!Settings->GetClass()->GetName().Contains(TEXT("TransformPoints")))
	{
		OutError = FString::Printf(TEXT("Node %d is '%s', not a PCGTransformPointsSettings node. Add a PCGTransformPointsSettings node first."),
			NodeIndex, *Settings->GetClass()->GetName());
		return;
	}

	// Properties that could not be found by reflection (renamed/removed in this PCG version).
	TArray<FString> MissingProps;

	auto SetVec = [&](const FString& PropName, const FVector& V) -> bool {
		FStructProperty* Prop = FindFProperty<FStructProperty>(Settings->GetClass(), *PropName);
		if (Prop && Prop->Struct == TBaseStructure<FVector>::Get())
		{
			*Prop->ContainerPtrToValuePtr<FVector>(Settings) = V;
			return true;
		}
		MissingProps.Add(PropName);
		return false;
	};
	auto SetRot = [&](const FString& PropName, const FRotator& R) -> bool {
		FStructProperty* Prop = FindFProperty<FStructProperty>(Settings->GetClass(), *PropName);
		if (Prop && Prop->Struct == TBaseStructure<FRotator>::Get())
		{
			*Prop->ContainerPtrToValuePtr<FRotator>(Settings) = R;
			return true;
		}
		MissingProps.Add(PropName);
		return false;
	};
	auto SetBool = [&](const FString& PropName, bool bVal) -> bool {
		FBoolProperty* Prop = FindFProperty<FBoolProperty>(Settings->GetClass(), *PropName);
		if (Prop)
		{
			Prop->SetPropertyValue(Prop->ContainerPtrToValuePtr<void>(Settings), bVal);
			return true;
		}
		MissingProps.Add(PropName);
		return false;
	};

	Settings->Modify();

	// Required: scale + rotation ranges. Missing any of these means the node is not a usable TransformPoints node.
	bool bRequiredOk = true;
	SetBool(TEXT("bUniformScale"), bUniformScale);
	bRequiredOk &= SetVec(TEXT("ScaleMin"), FVector(ScaleMin, ScaleMin, ScaleMin));
	bRequiredOk &= SetVec(TEXT("ScaleMax"), FVector(ScaleMax, ScaleMax, ScaleMax));

	// FRotator(Pitch, Yaw, Roll): rot_*_x = pitch, rot_*_z = yaw, rot_*_y = roll. Yaw-only by default.
	bRequiredOk &= SetRot(TEXT("RotationMin"), FRotator(RotMinX, RotMinZ, RotMinY));
	bRequiredOk &= SetRot(TEXT("RotationMax"), FRotator(RotMaxX, RotMaxZ, RotMaxY));
	if (!bRequiredOk)
	{
		OutError = FString::Printf(TEXT("Node %d (%s) is missing required TransformPoints properties: %s"),
			NodeIndex, *Settings->GetClass()->GetName(), *FString::Join(MissingProps, TEXT(", ")));
		return;
	}

	// Absolute rotation replaces the sampled surface rotation instead of composing with it,
	// so a yaw-only range produces upright instances even on slopes.
	SetBool(TEXT("bAbsoluteRotation"), bAbsoluteRotation);

	if (FMath::Abs(OffsetMinZ) > 0.01f || FMath::Abs(OffsetMaxZ) > 0.01f)
	{
		SetVec(TEXT("OffsetMin"), FVector(0.f, 0.f, OffsetMinZ));
		SetVec(TEXT("OffsetMax"), FVector(0.f, 0.f, OffsetMaxZ));
	}

	Settings->MarkPackageDirty();
	Graph->MarkPackageDirty();
	if (UWorld* PropWorld = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr)
	{
		if (UPCGSubsystem* PCGSub = PropWorld->GetSubsystem<UPCGSubsystem>())
			PCGSub->NotifyGraphChanged(Graph, EPCGChangeType::Settings);
	}

	TSharedPtr<FJsonObject> Out = MakeShareable(new FJsonObject);
	Out->SetBoolField(TEXT("success"), true);
	Out->SetStringField(TEXT("message"), FString::Printf(
		TEXT("Configured TransformPoints node[%d]: scale=[%.2f,%.2f] uniform=%s, yaw=[%.1f,%.1f]deg, pitch=[%.1f,%.1f]deg, roll=[%.1f,%.1f]deg, absolute_rotation=%s, offset_z=[%.1f,%.1f]cm."),
		NodeIndex, ScaleMin, ScaleMax, bUniformScale ? TEXT("true") : TEXT("false"),
		RotMinZ, RotMaxZ, RotMinX, RotMaxX, RotMinY, RotMaxY,
		bAbsoluteRotation ? TEXT("true") : TEXT("false"), OffsetMinZ, OffsetMaxZ));
	if (MissingProps.Num() > 0)
	{
		TArray<TSharedPtr<FJsonValue>> WarnArr;
		for (const FString& Missing : MissingProps)
			WarnArr.Add(MakeShareable(new FJsonValueString(FString::Printf(TEXT("Property '%s' not found on %s - skipped"), *Missing, *Settings->GetClass()->GetName()))));
		Out->SetArrayField(TEXT("warnings"), WarnArr);
	}

	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutJsonString);
	FJsonSerializer::Serialize(Out.ToSharedRef(), Writer);
}

void HandleSetPCGMeshSpawner(const FString& GraphPath, int32 NodeIndex, const TArray<FString>& MeshPaths, const TArray<float>& Weights, FString& OutJsonString, FString& OutError)
{

	UPCGGraph* Graph = LoadPCGGraph(GraphPath, OutError);
	if (!Graph) return;

	TArray<UPCGNode*> All = GetVirtualNodeList(Graph);
	if (NodeIndex < 0 || NodeIndex >= All.Num())
	{
		OutError = FString::Printf(TEXT("Node index %d out of range (%d nodes)"), NodeIndex, All.Num());
		return;
	}

	UPCGNode* Node = All[NodeIndex];
	UPCGSettings* Settings = Node ? Node->GetSettings() : nullptr;
	if (!Settings) { OutError = FString::Printf(TEXT("Node %d has no settings object"), NodeIndex); return; }

	FObjectProperty* SelectorProp = FindFProperty<FObjectProperty>(Settings->GetClass(), TEXT("MeshSelectorParameters"));
	if (!SelectorProp)
	{
		OutError = FString::Printf(TEXT("Node %d (%s) is not a PCGStaticMeshSpawner — no MeshSelectorParameters property found"), NodeIndex, *Settings->GetClass()->GetName());
		return;
	}

	UObject* SelectorObj = SelectorProp->GetObjectPropertyValue_InContainer(Settings);
	if (!SelectorObj) { OutError = TEXT("MeshSelectorParameters is null — node may not be a PCGStaticMeshSpawner"); return; }

	FArrayProperty* EntriesProp = FindFProperty<FArrayProperty>(SelectorObj->GetClass(), TEXT("MeshEntries"));
	if (!EntriesProp)
	{
		OutError = FString::Printf(TEXT("MeshSelector class '%s' has no MeshEntries array property"), *SelectorObj->GetClass()->GetName());
		return;
	}

	FStructProperty* EntryStructProp = CastField<FStructProperty>(EntriesProp->Inner);
	if (!EntryStructProp) { OutError = TEXT("MeshEntries inner element is not a struct"); return; }

	FStructProperty* DescriptorProp = FindFProperty<FStructProperty>(EntryStructProp->Struct, TEXT("Descriptor"));
	if (!DescriptorProp) { OutError = TEXT("MeshEntry struct has no 'Descriptor' property"); return; }

	FSoftObjectProperty* StaticMeshProp = FindFProperty<FSoftObjectProperty>(DescriptorProp->Struct, TEXT("StaticMesh"));
	if (!StaticMeshProp) { OutError = TEXT("Descriptor struct has no 'StaticMesh' soft object property"); return; }

	FIntProperty*   WeightIntProp   = FindFProperty<FIntProperty>(EntryStructProp->Struct, TEXT("Weight"));
	FFloatProperty* WeightFloatProp = FindFProperty<FFloatProperty>(EntryStructProp->Struct, TEXT("Weight"));

	void* ArrayPtr = EntriesProp->ContainerPtrToValuePtr<void>(SelectorObj);
	FScriptArrayHelper ArrayHelper(EntriesProp, ArrayPtr);
	ArrayHelper.EmptyValues();

	TArray<FString> AddedMeshes;
	for (int32 i = 0; i < MeshPaths.Num(); i++)
	{
		ArrayHelper.AddValue();
		void* EntryPtr = ArrayHelper.GetRawPtr(i);

		void* DescriptorPtr = DescriptorProp->ContainerPtrToValuePtr<void>(EntryPtr);
		void* StaticMeshPtr = StaticMeshProp->ContainerPtrToValuePtr<void>(DescriptorPtr);
		FString MeshPathStr = MeshPaths[i];
		if (!MeshPathStr.Contains(TEXT(".")))
		{
			int32 LastSlash = INDEX_NONE;
			MeshPathStr.FindLastChar(TEXT('/'), LastSlash);
			if (LastSlash != INDEX_NONE)
				MeshPathStr = MeshPathStr + TEXT(".") + MeshPathStr.RightChop(LastSlash + 1);
		}
		FSoftObjectPath SoftPath(*MeshPathStr);
		FSoftObjectPtr SoftPtr(SoftPath);
		StaticMeshProp->SetPropertyValue(StaticMeshPtr, SoftPtr);

		if (i < Weights.Num())
		{
			if (WeightIntProp)
			{
				void* WeightPtr = WeightIntProp->ContainerPtrToValuePtr<void>(EntryPtr);
				WeightIntProp->SetPropertyValue(WeightPtr, (int32)Weights[i]);
			}
			else if (WeightFloatProp)
			{
				void* WeightPtr = WeightFloatProp->ContainerPtrToValuePtr<void>(EntryPtr);
				WeightFloatProp->SetPropertyValue(WeightPtr, Weights[i]);
			}
		}

		AddedMeshes.Add(MeshPaths[i]);
	}

	SelectorObj->MarkPackageDirty();
	Graph->MarkPackageDirty();
	if (UWorld* MeshWorld = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr)
	{
		if (UPCGSubsystem* PCGSub = MeshWorld->GetSubsystem<UPCGSubsystem>())
		{
			PCGSub->NotifyGraphChanged(Graph, EPCGChangeType::Settings);
		}
	}

	TSharedPtr<FJsonObject> Out = MakeShareable(new FJsonObject);
	Out->SetBoolField(TEXT("success"), true);
	Out->SetNumberField(TEXT("mesh_count"), AddedMeshes.Num());
	Out->SetStringField(TEXT("message"), FString::Printf(TEXT("Configured %d mesh(es) on PCGStaticMeshSpawner node[%d]. Use spawn_pcg_actor or generate_pcg to run generation."), AddedMeshes.Num(), NodeIndex));

	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutJsonString);
	FJsonSerializer::Serialize(Out.ToSharedRef(), Writer);
}

void HandleGeneratePCG(const FString& ActorLabel, bool bForce, bool bForceOverBudget, FString& OutJsonString, FString& OutError)
{

	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!World) { OutError = TEXT("No editor world available"); return; }

	AActor* TargetActor = nullptr;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		if (It->GetActorLabel().Equals(ActorLabel, ESearchCase::IgnoreCase))
		{
			TargetActor = *It;
			break;
		}
	}

	if (!TargetActor) { OutError = FString::Printf(TEXT("No actor with label '%s' found in the current level"), *ActorLabel); return; }

	UPCGComponent* PCGComp = TargetActor->FindComponentByClass<UPCGComponent>();
	if (!PCGComp) { OutError = FString::Printf(TEXT("Actor '%s' has no PCGComponent"), *ActorLabel); return; }

	// Budget guard: if the level is ALREADY well over budget before we generate anything more,
	// refuse rather than pile on (this is exactly how "add biomes/grass/roads" turned into 16k+
	// stacked instances and a stalled editor). force_over_budget bypasses this.
	const int32 Budget = GetPCGInstanceBudget();
	{
		int32 PreExistingTotal = 0;
		for (const FPCGActorSummary& S : GatherPCGActorSummaries(World)) PreExistingTotal += S.TotalInstances;
		if (PreExistingTotal > Budget * 2 && !bForceOverBudget)
		{
			OutError = FString::Printf(
				TEXT("Level already has %d PCG instances (budget %d) BEFORE this generation — refusing to add more. ")
				TEXT("Call get_pcg_level_summary to see what's stacked, delete/merge overlapping PCG actors or reduce density, ")
				TEXT("or pass force_over_budget=true to override."),
				PreExistingTotal, Budget);
			return;
		}
	}

	UPCGGraph* Graph = PCGComp->GetGraph();
	UE_LOG(LogTemp, Log, TEXT("PCG Generate: Actor='%s', Graph='%s', HasGraph=%s"),
		*ActorLabel, Graph ? *Graph->GetName() : TEXT("null"), Graph ? TEXT("true") : TEXT("false"));

	if (Graph)
	{
		UE_LOG(LogTemp, Log, TEXT("PCG Generate: Graph has %d nodes (including Input/Output)"), Graph->GetNodes().Num() + 2);
	}

	UBoxComponent* BoxComp = TargetActor->FindComponentByClass<UBoxComponent>();
	if (BoxComp)
	{
		FVector Extent = BoxComp->GetScaledBoxExtent();
		FVector Loc = TargetActor->GetActorLocation();
		UE_LOG(LogTemp, Log, TEXT("PCG Generate: Volume at (%.0f,%.0f,%.0f) extent=(%.0f,%.0f,%.0f)"),
			Loc.X, Loc.Y, Loc.Z, Extent.X, Extent.Y, Extent.Z);
	}

	int32 LandscapeCount = 0;
	for (TActorIterator<ALandscapeProxy> It(World); It; ++It)
	{
		LandscapeCount++;
		FVector LOrigin, LExtent;
		(*It)->GetActorBounds(false, LOrigin, LExtent);
		UE_LOG(LogTemp, Log, TEXT("PCG Generate: Found landscape '%s' at (%.0f,%.0f,%.0f) extent=(%.0f,%.0f,%.0f)"),
			*(*It)->GetActorLabel(), LOrigin.X, LOrigin.Y, LOrigin.Z, LExtent.X, LExtent.Y, LExtent.Z);
	}
	if (LandscapeCount == 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("PCG Generate: No landscapes found in level — SurfaceSampler will have no surfaces to sample from!"));
	}

	if (Graph)
		PCGComp->SetGraph(Graph);

	PCGComp->Activate(true);
	PCGComp->GenerateLocal(bForce);

	if (UPCGSubsystem* PCGSub = World->GetSubsystem<UPCGSubsystem>())
	{
		const double WaitStart = FPlatformTime::Seconds();
		while (PCGComp->IsGenerating() && FPlatformTime::Seconds() - WaitStart < 10.0)
		{
			PCGSub->Tick(0.033f);
			FPlatformProcess::Sleep(0.033f);
		}
		UE_LOG(LogTemp, Log, TEXT("PCG Generate: waited %.2fs for generation to complete (IsGenerating=%s, bGenerated=%s)"),
			FPlatformTime::Seconds() - WaitStart,
			PCGComp->IsGenerating() ? TEXT("true") : TEXT("false"),
			PCGComp->bGenerated ? TEXT("true") : TEXT("false"));
	}

	int32 ISMComponentCount = 0;
	int32 TotalInstances = 0;
	PCGComp->ForEachManagedResource([&](UPCGManagedResource* Resource)
	{
		UPCGManagedComponent* ManagedComp = Cast<UPCGManagedComponent>(Resource);
		if (!ManagedComp) return;
		UActorComponent* ActorComp = ManagedComp->GeneratedComponent.Get();
		if (UInstancedStaticMeshComponent* ISMComp = Cast<UInstancedStaticMeshComponent>(ActorComp))
		{
			ISMComponentCount++;
			TotalInstances += ISMComp->GetInstanceCount();
		}
	});

	UE_LOG(LogTemp, Log, TEXT("PCG Generate: GenerateLocal completed for '%s' — %d ISM components, %d total instances"),
		*ActorLabel, ISMComponentCount, TotalInstances);

	if (ISMComponentCount == 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("PCG Generate: NO instances spawned! Check: (1) PCG graph has GetLandscapeData->SurfaceSampler->Spawner chain, (2) MeshSpawner has mesh entries, (3) Bounds overlap landscape, (4) Density > 0"));
	}

	// Level-wide picture after this generation: total instances, budget, and which other PCG
	// actors this one now overlaps (the same signal get_pcg_level_summary reports).
	TArray<FPCGActorSummary> PostActors = GatherPCGActorSummaries(World);
	int32 LevelTotalInstances = 0;
	for (const FPCGActorSummary& S : PostActors) LevelTotalInstances += S.TotalInstances;
	const bool bOverBudget = LevelTotalInstances > Budget;

	FVector TgtCenter, TgtExtent;
	GetPCGActorWorldAABB(TargetActor, TgtCenter, TgtExtent);
	TArray<TSharedPtr<FJsonValue>> OverlappingActorsJson;
	for (const FPCGActorSummary& S : PostActors)
	{
		if (S.Actor == TargetActor) continue;
		const float Fraction = ComputePCGBoxOverlapFractionXY(TgtCenter, TgtExtent, S.Center, S.Extent);
		if (Fraction < 0.25f) continue;
		TSharedPtr<FJsonObject> O = MakeShareable(new FJsonObject);
		O->SetStringField(TEXT("actor_label"), S.Label);
		O->SetStringField(TEXT("graph_path"), S.GraphPath);
		O->SetNumberField(TEXT("overlap_fraction"), Fraction);
		OverlappingActorsJson.Add(MakeShareable(new FJsonValueObject(O)));
	}

	TSharedPtr<FJsonObject> Out = MakeShareable(new FJsonObject);
	Out->SetBoolField(TEXT("success"), true);
	Out->SetStringField(TEXT("actor_label"), TargetActor->GetActorLabel());
	Out->SetNumberField(TEXT("ism_components"), ISMComponentCount);
	Out->SetNumberField(TEXT("total_instances"), TotalInstances);
	Out->SetNumberField(TEXT("level_total_instances"), LevelTotalInstances);
	Out->SetNumberField(TEXT("budget"), Budget);
	Out->SetBoolField(TEXT("over_budget"), bOverBudget);
	Out->SetArrayField(TEXT("overlapping_actors"), OverlappingActorsJson);
	if (ISMComponentCount == 0)
		Out->SetStringField(TEXT("warning"), TEXT("No instances spawned. Verify: graph edges connected, mesh entries assigned, bounds overlap landscape, density > 0."));
	Out->SetStringField(TEXT("message"), FString::Printf(TEXT("PCG generation on '%s': %d ISM components, %d instances spawned."), *ActorLabel, ISMComponentCount, TotalInstances));

	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutJsonString);
	FJsonSerializer::Serialize(Out.ToSharedRef(), Writer);
}

void HandleUpdatePCGActorGraph(const FString& ActorLabel, const FString& GraphPath, bool bGenerate, FString& OutJsonString, FString& OutError)
{

	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!World) { OutError = TEXT("No editor world available"); return; }

	UObject* Obj = UEditorAssetLibrary::LoadAsset(GraphPath);
	UPCGGraph* Graph = Cast<UPCGGraph>(Obj);
	if (!Graph) { OutError = FString::Printf(TEXT("Could not load PCGGraph at '%s'"), *GraphPath); return; }

	AActor* TargetActor = nullptr;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		if (It->GetActorLabel().Equals(ActorLabel, ESearchCase::IgnoreCase))
		{
			TargetActor = *It;
			break;
		}
	}

	if (!TargetActor) { OutError = FString::Printf(TEXT("No actor with label '%s' found in the current level"), *ActorLabel); return; }

	UPCGComponent* PCGComp = TargetActor->FindComponentByClass<UPCGComponent>();
	if (!PCGComp) { OutError = FString::Printf(TEXT("Actor '%s' has no PCGComponent"), *ActorLabel); return; }

	PCGComp->SetGraph(Graph);

	if (bGenerate)
	{
		PCGComp->GenerateLocal(true);
	}

	World->MarkPackageDirty();

	TSharedPtr<FJsonObject> Out = MakeShareable(new FJsonObject);
	Out->SetBoolField(TEXT("success"), true);
	Out->SetStringField(TEXT("actor_label"), TargetActor->GetActorLabel());
	Out->SetStringField(TEXT("graph_path"), GraphPath);
	Out->SetStringField(TEXT("message"), FString::Printf(TEXT("Updated PCGGraph on actor '%s' to '%s'%s."), *ActorLabel, *GraphPath, bGenerate ? TEXT(" and triggered generation") : TEXT("")));

	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutJsonString);
	FJsonSerializer::Serialize(Out.ToSharedRef(), Writer);
}

void HandleListPCGNodeTypes(FString& OutJsonString, FString& OutError)
{
	TArray<FString> NodeTypes;
	for (TObjectIterator<UClass> It; It; ++It)
	{
		if (!It->IsChildOf(UPCGSettings::StaticClass())) continue;
		if (*It == UPCGSettings::StaticClass()) continue;
		if (It->HasAnyClassFlags(CLASS_Abstract)) continue;
		NodeTypes.Add(It->GetName());
	}
	NodeTypes.Sort();

	TArray<TSharedPtr<FJsonValue>> Arr;
	for (const FString& Name : NodeTypes)
		Arr.Add(MakeShareable(new FJsonValueString(Name)));

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), true);
	Result->SetNumberField(TEXT("count"), NodeTypes.Num());
	Result->SetArrayField(TEXT("node_types"), Arr);
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutJsonString);
	FJsonSerializer::Serialize(Result.ToSharedRef(), Writer);
}

void HandleDisconnectPCGNodes(const FString& GraphPath, int32 FromNodeIndex, int32 ToNodeIndex, const FString& FromPin, const FString& ToPin, FString& OutJsonString, FString& OutError)
{

	UPCGGraph* Graph = LoadPCGGraph(GraphPath, OutError);
	if (!Graph) return;

	TArray<UPCGNode*> All = GetVirtualNodeList(Graph);

	if (FromNodeIndex < 0 || FromNodeIndex >= All.Num())
	{ OutError = FString::Printf(TEXT("from_node_index %d out of range (%d nodes)"), FromNodeIndex, All.Num()); return; }
	if (ToNodeIndex < 0 || ToNodeIndex >= All.Num())
	{ OutError = FString::Printf(TEXT("to_node_index %d out of range (%d nodes)"), ToNodeIndex, All.Num()); return; }

	UPCGNode* FromNode = All[FromNodeIndex];
	UPCGNode* ToNode   = All[ToNodeIndex];

	FName FromPinLabel = FromPin.IsEmpty() ? FName(TEXT("Out")) : FName(*FromPin);
	FName ToPinLabel   = ToPin.IsEmpty()   ? FName(TEXT("In"))  : FName(*ToPin);

	bool bRemoved = Graph->RemoveEdge(FromNode, FromPinLabel, ToNode, ToPinLabel);
	if (!bRemoved)
	{
		OutError = FString::Printf(
			TEXT("No edge found from node[%d].%s -> node[%d].%s. Use get_pcg_graph_summary to see existing edges."),
			FromNodeIndex, *FromPinLabel.ToString(), ToNodeIndex, *ToPinLabel.ToString());
		return;
	}

	Graph->MarkPackageDirty();
	if (UWorld* PCGWorld = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr)
	{
		if (UPCGSubsystem* Sub = PCGWorld->GetSubsystem<UPCGSubsystem>())
			Sub->NotifyGraphChanged(Graph, EPCGChangeType::Structural);
	}

	TSharedPtr<FJsonObject> Out = MakeShareable(new FJsonObject);
	Out->SetBoolField(TEXT("success"), true);
	Out->SetStringField(TEXT("message"), FString::Printf(TEXT("Removed edge node[%d].%s -> node[%d].%s"), FromNodeIndex, *FromPinLabel.ToString(), ToNodeIndex, *ToPinLabel.ToString()));
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutJsonString);
	FJsonSerializer::Serialize(Out.ToSharedRef(), Writer);
}

static FString FormatPropertyValue(FProperty* Prop, const void* DataPtr)
{
	if (FFloatProperty* P  = CastField<FFloatProperty>(Prop))  return FString::SanitizeFloat(P->GetPropertyValue(DataPtr));
	if (FDoubleProperty* P = CastField<FDoubleProperty>(Prop)) return FString::SanitizeFloat(P->GetPropertyValue(DataPtr));
	if (FIntProperty* P    = CastField<FIntProperty>(Prop))    return FString::FromInt(P->GetPropertyValue(DataPtr));
	if (FBoolProperty* P   = CastField<FBoolProperty>(Prop))   return P->GetPropertyValue(DataPtr) ? TEXT("true") : TEXT("false");
	if (FStrProperty* P    = CastField<FStrProperty>(Prop))    return P->GetPropertyValue(DataPtr);
	if (FNameProperty* P   = CastField<FNameProperty>(Prop))   return P->GetPropertyValue(DataPtr).ToString();
	if (FEnumProperty* P   = CastField<FEnumProperty>(Prop))
	{
		int64 Val = P->GetUnderlyingProperty()->GetSignedIntPropertyValue(DataPtr);
		FString Name = P->GetEnum()->GetNameStringByValue(Val);
		return Name.IsEmpty() ? FString::FromInt((int32)Val) : Name;
	}
	if (FByteProperty* P = CastField<FByteProperty>(Prop))
	{
		if (P->Enum)
		{
			FString Name = P->Enum->GetNameStringByValue(P->GetPropertyValue(DataPtr));
			return Name.IsEmpty() ? FString::FromInt(P->GetPropertyValue(DataPtr)) : Name;
		}
		return FString::FromInt(P->GetPropertyValue(DataPtr));
	}
	if (FStructProperty* P = CastField<FStructProperty>(Prop))
	{
		if (P->Struct == TBaseStructure<FVector>::Get())
		{
			const FVector& V = *reinterpret_cast<const FVector*>(DataPtr);
			return FString::Printf(TEXT("%.3f,%.3f,%.3f"), V.X, V.Y, V.Z);
		}
		if (P->Struct == TBaseStructure<FRotator>::Get())
		{
			const FRotator& R = *reinterpret_cast<const FRotator*>(DataPtr);
			return FString::Printf(TEXT("%.3f,%.3f,%.3f"), R.Pitch, R.Yaw, R.Roll);
		}
		if (P->Struct == TBaseStructure<FVector2D>::Get())
		{
			const FVector2D& V = *reinterpret_cast<const FVector2D*>(DataPtr);
			return FString::Printf(TEXT("%.3f,%.3f"), V.X, V.Y);
		}
		return TEXT("[struct]");
	}
	return TEXT("[unsupported]");
}

void HandleGetPCGNodeProperties(const FString& GraphPath, int32 NodeIndex, FString& OutJsonString, FString& OutError)
{

	UPCGGraph* Graph = LoadPCGGraph(GraphPath, OutError);
	if (!Graph) return;

	TArray<UPCGNode*> All = GetVirtualNodeList(Graph);
	if (NodeIndex < 0 || NodeIndex >= All.Num())
	{ OutError = FString::Printf(TEXT("Node index %d out of range (%d nodes)"), NodeIndex, All.Num()); return; }

	UPCGNode* Node = All[NodeIndex];
	UPCGSettings* Settings = Node ? Node->GetSettings() : nullptr;
	if (!Settings)
	{ OutError = FString::Printf(TEXT("Node %d has no settings object"), NodeIndex); return; }

	TSharedPtr<FJsonObject> PropsObj = MakeShareable(new FJsonObject);
	for (TFieldIterator<FProperty> It(Settings->GetClass()); It; ++It)
	{
		FProperty* Prop = *It;
		if (Prop->HasAnyPropertyFlags(CPF_Deprecated | CPF_Transient)) continue;

		const void* DataPtr = Prop->ContainerPtrToValuePtr<void>(Settings);
		FString ValStr = FormatPropertyValue(Prop, DataPtr);
		if (!ValStr.Equals(TEXT("[unsupported]")))
			PropsObj->SetStringField(Prop->GetName(), ValStr);
	}

	TSharedPtr<FJsonObject> Out = MakeShareable(new FJsonObject);
	Out->SetBoolField(TEXT("success"), true);
	Out->SetNumberField(TEXT("node_index"), NodeIndex);
	Out->SetStringField(TEXT("class"), Settings->GetClass()->GetName());
	Out->SetObjectField(TEXT("properties"), PropsObj);
	Out->SetStringField(TEXT("note"), TEXT("To change a value use set_pcg_node_property with float_value/int_value/bool_value/string_value."));

	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutJsonString);
	FJsonSerializer::Serialize(Out.ToSharedRef(), Writer);
}

void HandleBuildPCGGraph(const FString& GraphPath, const FString& NodesJson, const FString& EdgesJson, bool bClearExisting, FString& OutJsonString, FString& OutError)
{
	UE_LOG(LogTemp, Log, TEXT("PCG BuildGraph: path='%s', nodesJson=%d chars, edgesJson=%d chars, clear=%s"),
		*GraphPath, NodesJson.Len(), EdgesJson.Len(), bClearExisting ? TEXT("true") : TEXT("false"));
	if (NodesJson.Len() > 0)
		UE_LOG(LogTemp, Log, TEXT("PCG BuildGraph: nodesJson first 200 chars: %s"), *NodesJson.Left(200));

	UPCGGraph* Graph = LoadPCGGraph(GraphPath, OutError);
	if (!Graph) return;

	int32 ClearedCount = 0;
	if (bClearExisting)
	{
		TArray<UPCGNode*> NodesToRemove;
		for (UPCGNode* N : Graph->GetNodes())
		{
			if (N && N != Graph->GetInputNode() && N != Graph->GetOutputNode())
				NodesToRemove.Add(N);
		}
		for (UPCGNode* N : NodesToRemove)
			Graph->RemoveNode(N);
		ClearedCount = NodesToRemove.Num();
	}

	TMap<FString, UPCGNode*> NodeByName;
	NodeByName.Add(TEXT("Input"),  Graph->GetInputNode());
	NodeByName.Add(TEXT("Output"), Graph->GetOutputNode());

	TArray<TSharedPtr<FJsonValue>> CreatedNodes;
	TArray<FString> Errors;

	if (!NodesJson.IsEmpty())
	{
		TArray<TSharedPtr<FJsonValue>> NodeArr;
		TSharedRef<TJsonReader<>> NReader = TJsonReaderFactory<>::Create(NodesJson);
		FJsonSerializer::Deserialize(NReader, NodeArr);

		for (int32 i = 0; i < NodeArr.Num(); i++)
		{
			TSharedPtr<FJsonObject> NodeObj = NodeArr[i]->AsObject();
			if (!NodeObj.IsValid()) continue;

			FString ClassName, NodeName;
			double PosX = (double)(i * 250), PosY = 0.0;
			NodeObj->TryGetStringField(TEXT("class"),  ClassName);
			NodeObj->TryGetStringField(TEXT("name"),   NodeName);
			NodeObj->TryGetNumberField(TEXT("pos_x"),  PosX);
			NodeObj->TryGetNumberField(TEXT("pos_y"),  PosY);

			if (NodeName.IsEmpty()) NodeName = FString::Printf(TEXT("%s_%d"), *ClassName, i);

			if (ClassName.IsEmpty() || ClassName.Equals(TEXT("Input"), ESearchCase::IgnoreCase) ||
				ClassName.Equals(TEXT("Output"), ESearchCase::IgnoreCase))
			{
				continue;
			}

			UClass* Cls = FindPCGSettingsClass(ClassName);
			if (!Cls)
			{
				Errors.Add(FString::Printf(TEXT("Node '%s': class '%s' not found — call list_pcg_node_types to see valid names"), *NodeName, *ClassName));
				continue;
			}

			UPCGSettings* DefaultSettings = nullptr;
			UPCGNode* NewNode = Graph->AddNodeOfType(Cls, DefaultSettings);
			if (!NewNode)
			{
				Errors.Add(FString::Printf(TEXT("Node '%s': AddNodeOfType returned null"), *NodeName));
				continue;
			}

#if WITH_EDITOR
			NewNode->SetNodePosition((int32)PosX, (int32)PosY);
#endif

			NodeByName.Add(NodeName, NewNode);

			const TSharedPtr<FJsonObject>* PropsObjPtr = nullptr;
			if (NodeObj->TryGetObjectField(TEXT("properties"), PropsObjPtr) && PropsObjPtr && (*PropsObjPtr).IsValid())
			{
				if (UPCGSettings* Settings = NewNode->GetSettings())
				{
					for (auto& Pair : (*PropsObjPtr)->Values)
					{
						const FString PropName(*Pair.Key);
						FString PropErr;
						TSharedPtr<FJsonObject> ValObj = Pair.Value->AsObject();
						if (ValObj.IsValid())
						{
							if (!SetReflectionProperty(Settings, PropName, ValObj, PropErr))
								Errors.Add(FString::Printf(TEXT("Node '%s' prop '%s': %s"), *NodeName, *PropName, *PropErr));
						}
					}
					Settings->MarkPackageDirty();
				}
			}

			const TArray<TSharedPtr<FJsonValue>>* MeshArr = nullptr;
			if (NodeObj->TryGetArrayField(TEXT("mesh_paths"), MeshArr) && MeshArr)
			{
				TArray<FString> MeshPaths;
				const TArray<TSharedPtr<FJsonValue>>* WeightsArr = nullptr;
				TArray<float> Weights;
				for (const TSharedPtr<FJsonValue>& V : *MeshArr) { FString S; V->TryGetString(S); MeshPaths.Add(S); }
				if (NodeObj->TryGetArrayField(TEXT("weights"), WeightsArr) && WeightsArr)
					for (const TSharedPtr<FJsonValue>& V : *WeightsArr) { double W = 1.0; V->TryGetNumber(W); Weights.Add((float)W); }

				int32 VIdx = GetVirtualIndexOfNode(Graph, NewNode);
				FString MeshJson, MeshErr;
				HandleSetPCGMeshSpawner(GraphPath, VIdx, MeshPaths, Weights, MeshJson, MeshErr);
				if (!MeshErr.IsEmpty()) Errors.Add(FString::Printf(TEXT("Node '%s' mesh_paths: %s"), *NodeName, *MeshErr));
			}

			TSharedPtr<FJsonObject> CreatedObj = MakeShareable(new FJsonObject);
			CreatedObj->SetStringField(TEXT("name"),       NodeName);
			CreatedObj->SetStringField(TEXT("class"),      Cls->GetName());
			CreatedObj->SetNumberField(TEXT("node_index"), GetVirtualIndexOfNode(Graph, NewNode));
			CreatedNodes.Add(MakeShareable(new FJsonValueObject(CreatedObj)));
		}
	}

	TArray<TSharedPtr<FJsonValue>> CreatedEdges;
	if (!EdgesJson.IsEmpty())
	{
		TArray<TSharedPtr<FJsonValue>> EdgeArr;
		TSharedRef<TJsonReader<>> EReader = TJsonReaderFactory<>::Create(EdgesJson);
		FJsonSerializer::Deserialize(EReader, EdgeArr);

		for (const TSharedPtr<FJsonValue>& EV : EdgeArr)
		{
			TSharedPtr<FJsonObject> EdgeObj = EV->AsObject();
			if (!EdgeObj.IsValid()) continue;

			FString FromName, ToName, FromPin, ToPin;
			EdgeObj->TryGetStringField(TEXT("from"),      FromName);
			EdgeObj->TryGetStringField(TEXT("to"),        ToName);
			EdgeObj->TryGetStringField(TEXT("from_pin"),  FromPin);
			EdgeObj->TryGetStringField(TEXT("to_pin"),    ToPin);

			if (FromPin.IsEmpty()) FromPin = TEXT("Out");
			if (ToPin.IsEmpty())   ToPin   = TEXT("In");

			UPCGNode** FromNodePtr = NodeByName.Find(FromName);
			UPCGNode** ToNodePtr   = NodeByName.Find(ToName);

			if (!FromNodePtr) { Errors.Add(FString::Printf(TEXT("Edge: from node '%s' not found in this graph"), *FromName)); continue; }
			if (!ToNodePtr)   { Errors.Add(FString::Printf(TEXT("Edge: to node '%s' not found in this graph"), *ToName));   continue; }

			UPCGPin* OutPin = (*FromNodePtr)->GetOutputPin(FName(*FromPin));
			UPCGPin* InPin  = (*ToNodePtr)->GetInputPin(FName(*ToPin));
			if (!OutPin)
			{
				Errors.Add(FString::Printf(TEXT("Edge: from_pin '%s' not found on node '%s'"), *FromPin, *FromName));
			}
			else if (!InPin)
			{
				Errors.Add(FString::Printf(TEXT("Edge: to_pin '%s' not found on node '%s'"), *ToPin, *ToName));
			}
			else
			{
				bool bOk = OutPin->AddEdgeTo(InPin) != 0;
				if (!bOk)
				{
					Errors.Add(FString::Printf(TEXT("Edge: %s.%s -> %s.%s failed — pins may be incompatible"), *FromName, *FromPin, *ToName, *ToPin));
				}
				else
				{
					TSharedPtr<FJsonObject> EO = MakeShareable(new FJsonObject);
					EO->SetStringField(TEXT("from"), FromName + TEXT(".") + FromPin);
					EO->SetStringField(TEXT("to"),   ToName   + TEXT(".") + ToPin);
					CreatedEdges.Add(MakeShareable(new FJsonValueObject(EO)));
				}
			}
		}
	}

	Graph->MarkPackageDirty();

	if (UWorld* PCGWorld = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr)
	{
		if (UPCGSubsystem* Sub = PCGWorld->GetSubsystem<UPCGSubsystem>())
			Sub->NotifyGraphChanged(Graph, EPCGChangeType::Structural);
	}

	UE_LOG(LogTemp, Log, TEXT("PCG BuildGraph: DONE — %d nodes, %d edges, %d errors. Graph saved to disk."),
		CreatedNodes.Num(), CreatedEdges.Num(), Errors.Num());

	TSharedPtr<FJsonObject> Out = MakeShareable(new FJsonObject);
	Out->SetBoolField(TEXT("success"),  Errors.IsEmpty());
	Out->SetArrayField(TEXT("nodes_created"), CreatedNodes);
	Out->SetArrayField(TEXT("edges_created"), CreatedEdges);
	Out->SetNumberField(TEXT("node_count"),   CreatedNodes.Num());
	Out->SetNumberField(TEXT("edge_count"),   CreatedEdges.Num());
	Out->SetNumberField(TEXT("error_count"),  Errors.Num());
	if (ClearedCount > 0)
		Out->SetNumberField(TEXT("cleared_count"), ClearedCount);
	if (Errors.Num() > 0)
		Out->SetStringField(TEXT("errors"), FString::Join(Errors, TEXT("; ")));
	Out->SetStringField(TEXT("message"), FString::Printf(
		TEXT("build_pcg_graph complete: %s%d nodes added, %d edges wired, %d errors. Call get_pcg_graph_summary to verify indices."),
		ClearedCount > 0 ? *FString::Printf(TEXT("%d old nodes cleared, "), ClearedCount) : TEXT(""),
		CreatedNodes.Num(), CreatedEdges.Num(), Errors.Num()));

	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutJsonString);
	FJsonSerializer::Serialize(Out.ToSharedRef(), Writer);
}

void HandleSetPCGSlopeFilter(const FString& GraphPath, int32 NormalDensityNodeIndex, int32 DensityFilterNodeIndex, float MaxSlopeAngleDegrees, bool bInvert, FString& OutJsonString, FString& OutError)
{

	UPCGGraph* Graph = LoadPCGGraph(GraphPath, OutError);
	if (!Graph) return;

	TArray<UPCGNode*> All = GetVirtualNodeList(Graph);

	if (NormalDensityNodeIndex >= 0 && NormalDensityNodeIndex < All.Num())
	{
		UPCGNode* NDNode = All[NormalDensityNodeIndex];
		UPCGSettings* NDSettings = NDNode ? NDNode->GetSettings() : nullptr;
		if (!NDSettings)
		{
			OutError = FString::Printf(TEXT("Node[%d] has no settings"), NormalDensityNodeIndex);
			return;
		}
		FString PropErr;
		TSharedPtr<FJsonObject> NormalVal = MakeShareable(new FJsonObject);
		NormalVal->SetStringField(TEXT("string_value"), TEXT("0,0,1"));
		SetReflectionProperty(NDSettings, TEXT("Normal"), NormalVal, PropErr);

		TSharedPtr<FJsonObject> OffsetVal = MakeShareable(new FJsonObject);
		OffsetVal->SetNumberField(TEXT("float_value"), 0.0);
		SetReflectionProperty(NDSettings, TEXT("Offset"), OffsetVal, PropErr);

		TSharedPtr<FJsonObject> StrengthVal = MakeShareable(new FJsonObject);
		StrengthVal->SetNumberField(TEXT("float_value"), 1.0);
		SetReflectionProperty(NDSettings, TEXT("Strength"), StrengthVal, PropErr);

		TSharedPtr<FJsonObject> ModeVal = MakeShareable(new FJsonObject);
		ModeVal->SetStringField(TEXT("string_value"), TEXT("Set"));
		SetReflectionProperty(NDSettings, TEXT("DensityMode"), ModeVal, PropErr);

		NDSettings->MarkPackageDirty();
	}

	const float Threshold = FMath::Cos(FMath::DegreesToRadians(MaxSlopeAngleDegrees));

	if (DensityFilterNodeIndex >= 0 && DensityFilterNodeIndex < All.Num())
	{
		UPCGNode* DFNode = All[DensityFilterNodeIndex];
		if (UPCGDensityFilterSettings* DFSettings = Cast<UPCGDensityFilterSettings>(DFNode ? DFNode->GetSettings() : nullptr))
		{
			DFSettings->LowerBound = bInvert ? 0.0f  : Threshold;
			DFSettings->UpperBound = bInvert ? Threshold : 1.0f;
			DFSettings->bInvertFilter = false;
			DFSettings->MarkPackageDirty();
		}
		else
		{
			OutError = FString::Printf(TEXT("Node[%d] is not a PCGDensityFilterSettings node"), DensityFilterNodeIndex);
			return;
		}
	}

	Graph->MarkPackageDirty();

	TSharedPtr<FJsonObject> Out = MakeShareable(new FJsonObject);
	Out->SetBoolField(TEXT("success"), true);
	Out->SetNumberField(TEXT("threshold"), Threshold);
	Out->SetStringField(TEXT("message"), FString::Printf(
		TEXT("Slope filter set: max_slope=%g° (threshold=%.4f). %s terrain will be kept, %s terrain removed."),
		MaxSlopeAngleDegrees, Threshold,
		bInvert ? TEXT("Steep") : TEXT("Flat"),
		bInvert ? TEXT("flat") : TEXT("steep")));
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutJsonString);
	FJsonSerializer::Serialize(Out.ToSharedRef(), Writer);
}

void HandleSetPCGNoiseDensity(const FString& GraphPath, int32 NodeIndex, float Scale, float Brightness, float Contrast, const FString& Mode, int32 Iterations, FString& OutJsonString, FString& OutError)
{

	UPCGGraph* Graph = LoadPCGGraph(GraphPath, OutError);
	if (!Graph) return;

	TArray<UPCGNode*> All = GetVirtualNodeList(Graph);
	if (NodeIndex < 0 || NodeIndex >= All.Num())
	{ OutError = FString::Printf(TEXT("Node index %d out of range"), NodeIndex); return; }

	UPCGNode* Node = All[NodeIndex];
	UPCGSettings* Settings = Node ? Node->GetSettings() : nullptr;
	if (!Settings)
	{ OutError = FString::Printf(TEXT("Node[%d] has no settings"), NodeIndex); return; }

	FString PropErr;

	if (!Mode.IsEmpty())
	{
		TSharedPtr<FJsonObject> ModeVal = MakeShareable(new FJsonObject);
		ModeVal->SetStringField(TEXT("string_value"), Mode);
		SetReflectionProperty(Settings, TEXT("Mode"), ModeVal, PropErr);
	}

	{
		TSharedPtr<FJsonObject> Val = MakeShareable(new FJsonObject);
		Val->SetNumberField(TEXT("float_value"), Brightness);
		SetReflectionProperty(Settings, TEXT("Brightness"), Val, PropErr);
	}

	{
		TSharedPtr<FJsonObject> Val = MakeShareable(new FJsonObject);
		Val->SetNumberField(TEXT("float_value"), Contrast);
		SetReflectionProperty(Settings, TEXT("Contrast"), Val, PropErr);
	}

	if (Iterations > 0)
	{
		TSharedPtr<FJsonObject> Val = MakeShareable(new FJsonObject);
		Val->SetNumberField(TEXT("int_value"), FMath::Clamp(Iterations, 1, 100));
		SetReflectionProperty(Settings, TEXT("Iterations"), Val, PropErr);
	}

	FStructProperty* TransformProp = FindFProperty<FStructProperty>(Settings->GetClass(), TEXT("Transform"));
	if (TransformProp && TransformProp->Struct == TBaseStructure<FTransform>::Get())
	{
		FTransform* TransformPtr = TransformProp->ContainerPtrToValuePtr<FTransform>(Settings);
		if (TransformPtr)
			TransformPtr->SetScale3D(FVector(Scale, Scale, Scale));
	}

	Settings->MarkPackageDirty();
	Graph->MarkPackageDirty();

	TSharedPtr<FJsonObject> Out = MakeShareable(new FJsonObject);
	Out->SetBoolField(TEXT("success"), true);
	Out->SetStringField(TEXT("message"), FString::Printf(
		TEXT("SpatialNoise node[%d] configured: mode=%s scale=%.0f brightness=%.2f contrast=%.2f iterations=%d"),
		NodeIndex, *Mode, Scale, Brightness, Contrast, Iterations));
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutJsonString);
	FJsonSerializer::Serialize(Out.ToSharedRef(), Writer);
}

void HandleSetPCGSplineSampler(const FString& GraphPath, int32 NodeIndex, const FString& SamplingMode, float DistanceIncrement, int32 NumSamples, int32 SubdivisionsPerSegment, const FString& Dimension, FString& OutJsonString, FString& OutError)
{

	UPCGGraph* Graph = LoadPCGGraph(GraphPath, OutError);
	if (!Graph) return;

	TArray<UPCGNode*> All = GetVirtualNodeList(Graph);
	if (NodeIndex < 0 || NodeIndex >= All.Num())
	{ OutError = FString::Printf(TEXT("Node index %d out of range"), NodeIndex); return; }

	UPCGNode* Node = All[NodeIndex];
	UPCGSplineSamplerSettings* SamplerSettings = Cast<UPCGSplineSamplerSettings>(Node ? Node->GetSettings() : nullptr);
	if (!SamplerSettings)
	{ OutError = FString::Printf(TEXT("Node[%d] is not a PCGSplineSamplerSettings node"), NodeIndex); return; }

	if      (Dimension == TEXT("OnSpline"))    SamplerSettings->SamplerParams.Dimension = EPCGSplineSamplingDimension::OnSpline;
	else if (Dimension == TEXT("OnHorizontal")) SamplerSettings->SamplerParams.Dimension = EPCGSplineSamplingDimension::OnHorizontal;
	else if (Dimension == TEXT("OnVertical"))   SamplerSettings->SamplerParams.Dimension = EPCGSplineSamplingDimension::OnVertical;
	else if (Dimension == TEXT("OnVolume"))     SamplerSettings->SamplerParams.Dimension = EPCGSplineSamplingDimension::OnVolume;
	else if (Dimension == TEXT("OnInterior"))   SamplerSettings->SamplerParams.Dimension = EPCGSplineSamplingDimension::OnInterior;

	if      (SamplingMode == TEXT("Distance"))        SamplerSettings->SamplerParams.Mode = EPCGSplineSamplingMode::Distance;
	else if (SamplingMode == TEXT("Subdivision"))     SamplerSettings->SamplerParams.Mode = EPCGSplineSamplingMode::Subdivision;
	else if (SamplingMode == TEXT("NumberOfSamples")) SamplerSettings->SamplerParams.Mode = EPCGSplineSamplingMode::NumberOfSamples;

	if (DistanceIncrement > 0.f)    SamplerSettings->SamplerParams.DistanceIncrement    = DistanceIncrement;
	if (NumSamples > 0)             SamplerSettings->SamplerParams.NumSamples           = NumSamples;
	if (SubdivisionsPerSegment > 0) SamplerSettings->SamplerParams.SubdivisionsPerSegment = SubdivisionsPerSegment;

	SamplerSettings->MarkPackageDirty();
	Graph->MarkPackageDirty();

	TSharedPtr<FJsonObject> Out = MakeShareable(new FJsonObject);
	Out->SetBoolField(TEXT("success"), true);
	Out->SetStringField(TEXT("message"), FString::Printf(
		TEXT("SplineSampler node[%d] configured: dimension=%s mode=%s distance_increment=%.1f. Connect a DataFromActor node's 'Out' to this node's 'Spline' input pin."),
		NodeIndex, *Dimension, *SamplingMode, DistanceIncrement));
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutJsonString);
	FJsonSerializer::Serialize(Out.ToSharedRef(), Writer);
}

void HandleClearPCGGraph(const FString& GraphPath, FString& OutJsonString, FString& OutError)
{

	UPCGGraph* Graph = LoadPCGGraph(GraphPath, OutError);
	if (!Graph) return;

	TArray<UPCGNode*> NodesToRemove;
	for (UPCGNode* N : Graph->GetNodes())
	{
		if (N && N != Graph->GetInputNode() && N != Graph->GetOutputNode())
			NodesToRemove.Add(N);
	}

	for (UPCGNode* N : NodesToRemove)
		Graph->RemoveNode(N);

	Graph->MarkPackageDirty();

	TSharedPtr<FJsonObject> Out = MakeShareable(new FJsonObject);
	Out->SetBoolField(TEXT("success"), true);
	Out->SetNumberField(TEXT("removed_count"), NodesToRemove.Num());
	Out->SetStringField(TEXT("message"), FString::Printf(
		TEXT("Cleared %d nodes from PCG graph. Only Input[0] and Output[1] remain."), NodesToRemove.Num()));
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutJsonString);
	FJsonSerializer::Serialize(Out.ToSharedRef(), Writer);
}

void HandleSetPCGComponentProperties(const FString& ActorLabel, const TSharedPtr<FJsonObject>& PropertiesJson, FString& OutJsonString, FString& OutError)
{

	if (!PropertiesJson.IsValid()) { OutError = TEXT("No properties provided"); return; }

	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!World) { OutError = TEXT("No editor world available"); return; }

	AActor* TargetActor = nullptr;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		if (It->GetActorLabel().Equals(ActorLabel, ESearchCase::IgnoreCase)) { TargetActor = *It; break; }
	}
	if (!TargetActor) { OutError = FString::Printf(TEXT("No actor with label '%s' found"), *ActorLabel); return; }

	UPCGComponent* PCGComp = TargetActor->FindComponentByClass<UPCGComponent>();
	if (!PCGComp) { OutError = FString::Printf(TEXT("Actor '%s' has no PCGComponent"), *ActorLabel); return; }

	TArray<FString> Applied;

	double SeedVal;
	if (PropertiesJson->TryGetNumberField(TEXT("seed"), SeedVal))
	{
		PCGComp->Seed = (int32)SeedVal;
		Applied.Add(FString::Printf(TEXT("Seed=%d"), (int32)SeedVal));
	}

	bool bActivatedVal;
	if (PropertiesJson->TryGetBoolField(TEXT("activated"), bActivatedVal))
	{
		PCGComp->bActivated = bActivatedVal;
		Applied.Add(FString::Printf(TEXT("bActivated=%s"), bActivatedVal ? TEXT("true") : TEXT("false")));
	}

	bool bPartitionedVal;
	if (PropertiesJson->TryGetBoolField(TEXT("is_partitioned"), bPartitionedVal))
	{
		FBoolProperty* PartProp = FindFProperty<FBoolProperty>(UPCGComponent::StaticClass(), TEXT("bIsPartitioned"));
		if (PartProp)
			PartProp->SetPropertyValue(PartProp->ContainerPtrToValuePtr<void>(PCGComp), bPartitionedVal);
		Applied.Add(FString::Printf(TEXT("bIsPartitioned=%s"), bPartitionedVal ? TEXT("true") : TEXT("false")));
	}

	FString TriggerStr;
	bool bSetToGenerateOnLoad = false;
	if (PropertiesJson->TryGetStringField(TEXT("generation_trigger"), TriggerStr))
	{
		if (TriggerStr == TEXT("GenerateOnLoad"))
		{
			PCGComp->GenerationTrigger = EPCGComponentGenerationTrigger::GenerateOnLoad;
			bSetToGenerateOnLoad = true;
		}
		else if (TriggerStr == TEXT("GenerateOnDemand"))
			PCGComp->GenerationTrigger = EPCGComponentGenerationTrigger::GenerateOnDemand;
		else if (TriggerStr == TEXT("GenerateAtRuntime"))
			PCGComp->GenerationTrigger = EPCGComponentGenerationTrigger::GenerateAtRuntime;
		else
		{
			OutError = FString::Printf(TEXT("Invalid generation_trigger '%s'. Valid: GenerateOnLoad, GenerateOnDemand, GenerateAtRuntime"), *TriggerStr);
			return;
		}
		Applied.Add(FString::Printf(TEXT("GenerationTrigger=%s"), *TriggerStr));
	}

	FString InputTypeStr;
	if (PropertiesJson->TryGetStringField(TEXT("input_type"), InputTypeStr))
	{
		if (InputTypeStr == TEXT("Actor"))
			PCGComp->InputType = EPCGComponentInput::Actor;
		else if (InputTypeStr == TEXT("Landscape"))
			PCGComp->InputType = EPCGComponentInput::Landscape;
		else
		{
			OutError = FString::Printf(TEXT("Invalid input_type '%s'. Valid: Actor, Landscape"), *InputTypeStr);
			return;
		}
		Applied.Add(FString::Printf(TEXT("InputType=%s"), *InputTypeStr));
	}

	if (Applied.Num() == 0) { OutError = TEXT("No valid properties specified. Use: seed, activated, is_partitioned, generation_trigger, input_type"); return; }

	PCGComp->MarkPackageDirty();
	World->MarkPackageDirty();

	TSharedPtr<FJsonObject> Out = MakeShareable(new FJsonObject);
	Out->SetBoolField(TEXT("success"), true);
	Out->SetStringField(TEXT("actor_label"), ActorLabel);
	Out->SetStringField(TEXT("applied"), FString::Join(Applied, TEXT(", ")));
	if (bSetToGenerateOnLoad)
		Out->SetStringField(TEXT("note"), TEXT("GenerateOnLoad regenerates every time the level loads (slow for big worlds); use GenerateOnDemand + generate_pcg for baked content."));
	Out->SetStringField(TEXT("message"), FString::Printf(TEXT("Set %d properties on PCG component: %s"), Applied.Num(), *FString::Join(Applied, TEXT(", "))));
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutJsonString);
	FJsonSerializer::Serialize(Out.ToSharedRef(), Writer);
}

void HandleDuplicatePCGNode(const FString& GraphPath, int32 SourceNodeIndex, int32 PosX, int32 PosY, FString& OutJsonString, FString& OutError)
{

	UPCGGraph* Graph = LoadPCGGraph(GraphPath, OutError);
	if (!Graph) return;

	if (SourceNodeIndex == 0 || SourceNodeIndex == 1)
	{
		OutError = TEXT("Cannot duplicate the built-in Input (0) or Output (1) node");
		return;
	}

	TArray<UPCGNode*> All = GetVirtualNodeList(Graph);
	if (SourceNodeIndex < 0 || SourceNodeIndex >= All.Num())
	{
		OutError = FString::Printf(TEXT("Source node index %d out of range (%d nodes)"), SourceNodeIndex, All.Num());
		return;
	}

	UPCGNode* SourceNode = All[SourceNodeIndex];
	UPCGSettings* SourceSettings = SourceNode ? SourceNode->GetSettings() : nullptr;
	if (!SourceSettings)
	{
		OutError = FString::Printf(TEXT("Source node %d has no settings"), SourceNodeIndex);
		return;
	}

	UPCGSettings* NewDefaultSettings = nullptr;
	UPCGNode* NewNode = Graph->AddNodeOfType(SourceSettings->GetClass(), NewDefaultSettings);
	if (!NewNode) { OutError = TEXT("AddNodeOfType returned null"); return; }

	UPCGSettings* NewSettings = NewNode->GetSettings();
	if (NewSettings)
	{
		for (TFieldIterator<FProperty> It(SourceSettings->GetClass()); It; ++It)
		{
			FProperty* Prop = *It;
			if (Prop->HasAnyPropertyFlags(CPF_Deprecated | CPF_Transient)) continue;
			const void* SrcPtr = Prop->ContainerPtrToValuePtr<void>(SourceSettings);
			void* DstPtr = Prop->ContainerPtrToValuePtr<void>(NewSettings);
			Prop->CopyCompleteValue(DstPtr, SrcPtr);
		}
	}

#if WITH_EDITOR
	if (PosX == 0 && PosY == 0)
	{
		int32 SrcX = 0, SrcY = 0;
		SourceNode->GetNodePosition(SrcX, SrcY);
		NewNode->SetNodePosition(SrcX + 50, SrcY + 50);
	}
	else
	{
		NewNode->SetNodePosition(PosX, PosY);
	}
#endif

	Graph->MarkPackageDirty();

	int32 NewIndex = GetVirtualIndexOfNode(Graph, NewNode);

	TSharedPtr<FJsonObject> Out = MakeShareable(new FJsonObject);
	Out->SetBoolField(TEXT("success"), true);
	Out->SetNumberField(TEXT("node_index"), NewIndex);
	Out->SetStringField(TEXT("class"), SourceSettings->GetClass()->GetName());
	Out->SetStringField(TEXT("message"), FString::Printf(
		TEXT("Duplicated node[%d] (%s) → new node[%d] with all properties copied."),
		SourceNodeIndex, *SourceSettings->GetClass()->GetName(), NewIndex));
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutJsonString);
	FJsonSerializer::Serialize(Out.ToSharedRef(), Writer);
}

void HandleSetPCGNodeComment(const FString& GraphPath, int32 NodeIndex, const FString& Comment, bool bPinBubble, FString& OutJsonString, FString& OutError)
{

	UPCGGraph* Graph = LoadPCGGraph(GraphPath, OutError);
	if (!Graph) return;

	TArray<UPCGNode*> All = GetVirtualNodeList(Graph);
	if (NodeIndex < 0 || NodeIndex >= All.Num())
	{
		OutError = FString::Printf(TEXT("Node index %d out of range (%d nodes)"), NodeIndex, All.Num());
		return;
	}

	UPCGNode* Node = All[NodeIndex];
	Node->NodeComment = Comment;
	Node->bCommentBubbleVisible = !Comment.IsEmpty();
	Node->bCommentBubblePinned = bPinBubble;

	Graph->MarkPackageDirty();

	TSharedPtr<FJsonObject> Out = MakeShareable(new FJsonObject);
	Out->SetBoolField(TEXT("success"), true);
	Out->SetStringField(TEXT("message"), FString::Printf(
		TEXT("Set comment on node[%d]: '%s' (bubble %s)"),
		NodeIndex, *Comment, bPinBubble ? TEXT("pinned") : TEXT("visible")));
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutJsonString);
	FJsonSerializer::Serialize(Out.ToSharedRef(), Writer);
}

void HandleAddPCGGraphParameter(const FString& GraphPath, const FString& ParamName, const FString& ParamType, FString& OutJsonString, FString& OutError)
{

	UPCGGraph* Graph = LoadPCGGraph(GraphPath, OutError);
	if (!Graph) return;

	if (ParamName.IsEmpty()) { OutError = TEXT("'param_name' is required"); return; }

	EPropertyBagPropertyType BagType = EPropertyBagPropertyType::Double;
	if      (ParamType.Equals(TEXT("Bool"),   ESearchCase::IgnoreCase)) BagType = EPropertyBagPropertyType::Bool;
	else if (ParamType.Equals(TEXT("Int32"),  ESearchCase::IgnoreCase) || ParamType.Equals(TEXT("Int"), ESearchCase::IgnoreCase)) BagType = EPropertyBagPropertyType::Int32;
	else if (ParamType.Equals(TEXT("Int64"),  ESearchCase::IgnoreCase)) BagType = EPropertyBagPropertyType::Int64;
	else if (ParamType.Equals(TEXT("Float"),  ESearchCase::IgnoreCase)) BagType = EPropertyBagPropertyType::Float;
	else if (ParamType.Equals(TEXT("Double"), ESearchCase::IgnoreCase)) BagType = EPropertyBagPropertyType::Double;
	else if (ParamType.Equals(TEXT("Name"),   ESearchCase::IgnoreCase)) BagType = EPropertyBagPropertyType::Name;
	else if (ParamType.Equals(TEXT("String"), ESearchCase::IgnoreCase)) BagType = EPropertyBagPropertyType::String;
	else if (ParamType.Equals(TEXT("Byte"),   ESearchCase::IgnoreCase)) BagType = EPropertyBagPropertyType::Byte;
	else if (!ParamType.IsEmpty())
	{
		OutError = FString::Printf(TEXT("Unknown param_type '%s'. Valid: Bool, Int32, Int64, Float, Double, Name, String, Byte"), *ParamType);
		return;
	}

	TArray<FPropertyBagPropertyDesc> Descs;
	Descs.Add(FPropertyBagPropertyDesc(FName(*ParamName), BagType));
	Graph->AddUserParameters(Descs);
	Graph->MarkPackageDirty();

	TSharedPtr<FJsonObject> Out = MakeShareable(new FJsonObject);
	Out->SetBoolField(TEXT("success"), true);
	Out->SetStringField(TEXT("param_name"), ParamName);
	Out->SetStringField(TEXT("param_type"), ParamType.IsEmpty() ? TEXT("Double") : ParamType);
	Out->SetStringField(TEXT("message"), FString::Printf(TEXT("Added parameter '%s' (type: %s) to PCG graph. Use UserParameterGet node to read it in the graph."), *ParamName, *ParamType));
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutJsonString);
	FJsonSerializer::Serialize(Out.ToSharedRef(), Writer);
}

void HandleGetPCGGraphParameters(const FString& GraphPath, FString& OutJsonString, FString& OutError)
{

	UPCGGraph* Graph = LoadPCGGraph(GraphPath, OutError);
	if (!Graph) return;

	const FInstancedPropertyBag* UserParams = Graph->GetUserParametersStruct();

	TArray<TSharedPtr<FJsonValue>> ParamsArr;
	if (UserParams && UserParams->GetPropertyBagStruct())
	{
		for (const FPropertyBagPropertyDesc& Desc : UserParams->GetPropertyBagStruct()->GetPropertyDescs())
		{
			TSharedPtr<FJsonObject> ParamObj = MakeShareable(new FJsonObject);
			ParamObj->SetStringField(TEXT("name"), Desc.Name.ToString());

			FString TypeStr;
			switch (Desc.ValueType)
			{
			case EPropertyBagPropertyType::Bool:    TypeStr = TEXT("Bool"); break;
			case EPropertyBagPropertyType::Byte:    TypeStr = TEXT("Byte"); break;
			case EPropertyBagPropertyType::Int32:   TypeStr = TEXT("Int32"); break;
			case EPropertyBagPropertyType::Int64:   TypeStr = TEXT("Int64"); break;
			case EPropertyBagPropertyType::Float:   TypeStr = TEXT("Float"); break;
			case EPropertyBagPropertyType::Double:  TypeStr = TEXT("Double"); break;
			case EPropertyBagPropertyType::Name:    TypeStr = TEXT("Name"); break;
			case EPropertyBagPropertyType::String:  TypeStr = TEXT("String"); break;
			default: TypeStr = TEXT("Other"); break;
			}
			ParamObj->SetStringField(TEXT("type"), TypeStr);

			const FProperty* Prop = UserParams->FindPropertyDescByName(Desc.Name) ? UserParams->GetPropertyBagStruct()->FindPropertyByName(Desc.Name) : nullptr;
			if (Prop)
			{
				const void* DataPtr = Prop->ContainerPtrToValuePtr<void>(UserParams->GetValue().GetMemory());
				ParamObj->SetStringField(TEXT("value"), FormatPropertyValue(const_cast<FProperty*>(Prop), DataPtr));
			}

			ParamsArr.Add(MakeShareable(new FJsonValueObject(ParamObj)));
		}
	}

	TSharedPtr<FJsonObject> Out = MakeShareable(new FJsonObject);
	Out->SetBoolField(TEXT("success"), true);
	Out->SetNumberField(TEXT("count"), ParamsArr.Num());
	Out->SetArrayField(TEXT("parameters"), ParamsArr);
	Out->SetStringField(TEXT("message"), FString::Printf(TEXT("Graph has %d user parameter(s)."), ParamsArr.Num()));
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutJsonString);
	FJsonSerializer::Serialize(Out.ToSharedRef(), Writer);
}

void HandleSetPCGTextureSampler(const FString& GraphPath, int32 NodeIndex, const FString& TexturePath,
	const FString& ColorChannel, float TexelSize, bool bUseAdvancedTiling,
	float TilingX, float TilingY, FString& OutJsonString, FString& OutError)
{

	UPCGGraph* Graph = LoadPCGGraph(GraphPath, OutError);
	if (!Graph) return;

	TArray<UPCGNode*> All = GetVirtualNodeList(Graph);
	if (NodeIndex < 0 || NodeIndex >= All.Num())
	{ OutError = FString::Printf(TEXT("Node index %d out of range"), NodeIndex); return; }

	UPCGNode* Node = All[NodeIndex];
	UPCGSettings* Settings = Node ? Node->GetSettings() : nullptr;
	if (!Settings || !Settings->GetClass()->GetName().Contains(TEXT("TextureSampler")))
	{
		OutError = FString::Printf(TEXT("Node[%d] is not a PCGTextureSamplerSettings node (is: %s)"),
			NodeIndex, Settings ? *Settings->GetClass()->GetName() : TEXT("null"));
		return;
	}

	TArray<FString> Applied;

	if (!TexturePath.IsEmpty())
	{
		FSoftObjectProperty* TexProp = FindFProperty<FSoftObjectProperty>(Settings->GetClass(), TEXT("Texture"));
		if (TexProp)
		{
			void* TexPtr = TexProp->ContainerPtrToValuePtr<void>(Settings);
			FSoftObjectPath SoftPath(*TexturePath);
			FSoftObjectPtr SoftPtr(SoftPath);
			TexProp->SetPropertyValue(TexPtr, SoftPtr);
			Applied.Add(FString::Printf(TEXT("Texture=%s"), *TexturePath));
		}
	}

	if (!ColorChannel.IsEmpty())
	{
		FString PropErr;
		TSharedPtr<FJsonObject> Val = MakeShareable(new FJsonObject);
		Val->SetStringField(TEXT("string_value"), ColorChannel);
		if (SetReflectionProperty(Settings, TEXT("ColorChannel"), Val, PropErr))
			Applied.Add(FString::Printf(TEXT("ColorChannel=%s"), *ColorChannel));
		else
			OutError = FString::Printf(TEXT("ColorChannel: %s"), *PropErr);
	}

	if (TexelSize > 0.f)
	{
		FString PropErr;
		TSharedPtr<FJsonObject> Val = MakeShareable(new FJsonObject);
		Val->SetNumberField(TEXT("float_value"), TexelSize);
		if (SetReflectionProperty(Settings, TEXT("TexelSize"), Val, PropErr))
			Applied.Add(FString::Printf(TEXT("TexelSize=%.1f"), TexelSize));
	}

	if (bUseAdvancedTiling)
	{
		FString PropErr;
		TSharedPtr<FJsonObject> BoolVal = MakeShareable(new FJsonObject);
		BoolVal->SetBoolField(TEXT("bool_value"), true);
		SetReflectionProperty(Settings, TEXT("bUseAdvancedTiling"), BoolVal, PropErr);

		TSharedPtr<FJsonObject> TilingVal = MakeShareable(new FJsonObject);
		TilingVal->SetStringField(TEXT("string_value"), FString::Printf(TEXT("%f,%f"), TilingX, TilingY));
		SetReflectionProperty(Settings, TEXT("Tiling"), TilingVal, PropErr);
		Applied.Add(FString::Printf(TEXT("Tiling=%.1f,%.1f"), TilingX, TilingY));
	}

	Settings->MarkPackageDirty();
	Graph->MarkPackageDirty();

	TSharedPtr<FJsonObject> Out = MakeShareable(new FJsonObject);
	Out->SetBoolField(TEXT("success"), true);
	Out->SetStringField(TEXT("applied"), FString::Join(Applied, TEXT(", ")));
	Out->SetStringField(TEXT("message"), FString::Printf(TEXT("TextureSampler node[%d] configured: %s"), NodeIndex, *FString::Join(Applied, TEXT(", "))));
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutJsonString);
	FJsonSerializer::Serialize(Out.ToSharedRef(), Writer);
}

void HandleSetPCGDataFromActor(const FString& GraphPath, int32 NodeIndex,
	const FString& ActorFilter, const FString& ActorSelection, const FString& ActorSelectionTag,
	const FString& ActorSelectionClass, const FString& Mode,
	FString& OutJsonString, FString& OutError)
{

	UPCGGraph* Graph = LoadPCGGraph(GraphPath, OutError);
	if (!Graph) return;

	TArray<UPCGNode*> All = GetVirtualNodeList(Graph);
	if (NodeIndex < 0 || NodeIndex >= All.Num())
	{ OutError = FString::Printf(TEXT("Node index %d out of range"), NodeIndex); return; }

	UPCGNode* Node = All[NodeIndex];
	UPCGDataFromActorSettings* DFASettings = Cast<UPCGDataFromActorSettings>(Node ? Node->GetSettings() : nullptr);
	if (!DFASettings)
	{
		OutError = FString::Printf(TEXT("Node[%d] is not a PCGDataFromActorSettings node"), NodeIndex);
		return;
	}

	TArray<FString> Applied;

	if (!ActorFilter.IsEmpty())
	{
		if      (ActorFilter == TEXT("Self"))           DFASettings->ActorSelector.ActorFilter = EPCGActorFilter::Self;
		else if (ActorFilter == TEXT("Parent"))         DFASettings->ActorSelector.ActorFilter = EPCGActorFilter::Parent;
		else if (ActorFilter == TEXT("Root"))           DFASettings->ActorSelector.ActorFilter = EPCGActorFilter::Root;
		else if (ActorFilter == TEXT("AllWorldActors")) DFASettings->ActorSelector.ActorFilter = EPCGActorFilter::AllWorldActors;
		else if (ActorFilter == TEXT("Original"))       DFASettings->ActorSelector.ActorFilter = EPCGActorFilter::Original;
		else { OutError = FString::Printf(TEXT("Invalid actor_filter '%s'. Valid: Self, Parent, Root, AllWorldActors, Original"), *ActorFilter); return; }
		Applied.Add(FString::Printf(TEXT("ActorFilter=%s"), *ActorFilter));
	}

	if (!ActorSelection.IsEmpty())
	{
		if      (ActorSelection == TEXT("ByTag"))   DFASettings->ActorSelector.ActorSelection = EPCGActorSelection::ByTag;
		else if (ActorSelection == TEXT("ByClass")) DFASettings->ActorSelector.ActorSelection = EPCGActorSelection::ByClass;
		else { OutError = FString::Printf(TEXT("Invalid actor_selection '%s'. Valid: ByTag, ByClass"), *ActorSelection); return; }
		Applied.Add(FString::Printf(TEXT("ActorSelection=%s"), *ActorSelection));
	}

	if (!ActorSelectionTag.IsEmpty())
	{
		DFASettings->ActorSelector.ActorSelectionTag = FName(*ActorSelectionTag);
		Applied.Add(FString::Printf(TEXT("ActorSelectionTag=%s"), *ActorSelectionTag));
	}

	if (!ActorSelectionClass.IsEmpty())
	{
		UClass* Cls = FindObject<UClass>(nullptr, *ActorSelectionClass);
		if (!Cls)
		{
			Cls = FindObject<UClass>(nullptr, *(TEXT("/Script/Engine.") + ActorSelectionClass));
		}
		if (Cls)
		{
			DFASettings->ActorSelector.ActorSelectionClass = Cls;
			Applied.Add(FString::Printf(TEXT("ActorSelectionClass=%s"), *ActorSelectionClass));
		}
	}

	if (!Mode.IsEmpty())
	{
		if      (Mode == TEXT("ParseActorComponents"))    DFASettings->Mode = EPCGGetDataFromActorMode::ParseActorComponents;
		else if (Mode == TEXT("GetSinglePoint"))          DFASettings->Mode = EPCGGetDataFromActorMode::GetSinglePoint;
		else if (Mode == TEXT("GetDataFromProperty"))     DFASettings->Mode = EPCGGetDataFromActorMode::GetDataFromProperty;
		else if (Mode == TEXT("GetDataFromPCGComponent")) DFASettings->Mode = EPCGGetDataFromActorMode::GetDataFromPCGComponent;
#if !UE_VERSION_OLDER_THAN(5, 5, 0)
		else if (Mode == TEXT("GetActorReference"))       DFASettings->Mode = EPCGGetDataFromActorMode::GetActorReference;
#endif
		else
		{
#if !UE_VERSION_OLDER_THAN(5, 5, 0)
			OutError = FString::Printf(TEXT("Invalid mode '%s'. Valid: ParseActorComponents, GetSinglePoint, GetDataFromProperty, GetDataFromPCGComponent, GetActorReference"), *Mode);
#else
			OutError = FString::Printf(TEXT("Invalid mode '%s'. Valid: ParseActorComponents, GetSinglePoint, GetDataFromProperty, GetDataFromPCGComponent (GetActorReference requires UE 5.5+)"), *Mode);
#endif
			return;
		}
		Applied.Add(FString::Printf(TEXT("Mode=%s"), *Mode));
	}

	if (Applied.Num() == 0) { OutError = TEXT("No properties specified. Use: actor_filter, actor_selection, actor_selection_tag, actor_selection_class, mode"); return; }

	DFASettings->MarkPackageDirty();
	Graph->MarkPackageDirty();

	TSharedPtr<FJsonObject> Out = MakeShareable(new FJsonObject);
	Out->SetBoolField(TEXT("success"), true);
	Out->SetStringField(TEXT("applied"), FString::Join(Applied, TEXT(", ")));
	Out->SetStringField(TEXT("message"), FString::Printf(TEXT("DataFromActor node[%d] configured: %s"), NodeIndex, *FString::Join(Applied, TEXT(", "))));
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutJsonString);
	FJsonSerializer::Serialize(Out.ToSharedRef(), Writer);
}

void HandleSetPCGAttributeFilter(const FString& GraphPath, int32 NodeIndex,
	const FString& TargetAttribute, const FString& Operator, bool bUseConstantThreshold,
	const FString& ThresholdAttribute, float ThresholdConstant,
	FString& OutJsonString, FString& OutError)
{

	UPCGGraph* Graph = LoadPCGGraph(GraphPath, OutError);
	if (!Graph) return;

	TArray<UPCGNode*> All = GetVirtualNodeList(Graph);
	if (NodeIndex < 0 || NodeIndex >= All.Num())
	{ OutError = FString::Printf(TEXT("Node index %d out of range"), NodeIndex); return; }

	UPCGNode* Node = All[NodeIndex];
	UPCGAttributeFilteringSettings* FilterSettings = Cast<UPCGAttributeFilteringSettings>(Node ? Node->GetSettings() : nullptr);
	if (!FilterSettings)
	{
		OutError = FString::Printf(TEXT("Node[%d] is not a PCGAttributeFilteringSettings node (class: %s)"),
			NodeIndex, Node && Node->GetSettings() ? *Node->GetSettings()->GetClass()->GetName() : TEXT("null"));
		return;
	}

	TArray<FString> Applied;

	if (!Operator.IsEmpty())
	{
		if      (Operator == TEXT(">")  || Operator == TEXT("Greater"))        FilterSettings->Operator = EPCGAttributeFilterOperator::Greater;
		else if (Operator == TEXT(">=") || Operator == TEXT("GreaterOrEqual")) FilterSettings->Operator = EPCGAttributeFilterOperator::GreaterOrEqual;
		else if (Operator == TEXT("<")  || Operator == TEXT("Lesser"))         FilterSettings->Operator = EPCGAttributeFilterOperator::Lesser;
		else if (Operator == TEXT("<=") || Operator == TEXT("LesserOrEqual"))  FilterSettings->Operator = EPCGAttributeFilterOperator::LesserOrEqual;
		else if (Operator == TEXT("=")  || Operator == TEXT("Equal"))          FilterSettings->Operator = EPCGAttributeFilterOperator::Equal;
		else if (Operator == TEXT("!=") || Operator == TEXT("NotEqual"))       FilterSettings->Operator = EPCGAttributeFilterOperator::NotEqual;
		else if (Operator == TEXT("Substring"))                                FilterSettings->Operator = EPCGAttributeFilterOperator::Substring;
		else if (Operator == TEXT("Matches"))                                  FilterSettings->Operator = EPCGAttributeFilterOperator::Matches;
		else { OutError = FString::Printf(TEXT("Invalid operator '%s'. Valid: >, >=, <, <=, =, !=, Substring, Matches"), *Operator); return; }
		Applied.Add(FString::Printf(TEXT("Operator=%s"), *Operator));
	}

	if (!TargetAttribute.IsEmpty())
	{
		FString PropErr;
		FStructProperty* TargetProp = FindFProperty<FStructProperty>(FilterSettings->GetClass(), TEXT("TargetAttribute"));
		if (TargetProp)
		{
			void* StructPtr = TargetProp->ContainerPtrToValuePtr<void>(FilterSettings);
			FNameProperty* NameProp = FindFProperty<FNameProperty>(TargetProp->Struct, TEXT("AttributeName"));
			if (!NameProp)
				NameProp = FindFProperty<FNameProperty>(TargetProp->Struct, TEXT("Name"));
			if (NameProp)
			{
				void* NamePtr = NameProp->ContainerPtrToValuePtr<void>(StructPtr);
				NameProp->SetPropertyValue(NamePtr, FName(*TargetAttribute));
				Applied.Add(FString::Printf(TEXT("TargetAttribute=%s"), *TargetAttribute));
			}
			else
			{
				FString ImportStr = FString::Printf(TEXT("(Selection=(AttributeName=\"%s\"))"), *TargetAttribute);
				TargetProp->ImportText_Direct(*ImportStr, StructPtr, FilterSettings, PPF_None);
				Applied.Add(FString::Printf(TEXT("TargetAttribute=%s (via ImportText)"), *TargetAttribute));
			}
		}
	}

	FilterSettings->bUseConstantThreshold = bUseConstantThreshold;
	Applied.Add(FString::Printf(TEXT("bUseConstantThreshold=%s"), bUseConstantThreshold ? TEXT("true") : TEXT("false")));

	if (bUseConstantThreshold)
	{
		UScriptStruct* AttrTypesStruct = FPCGMetadataTypesConstantStruct::StaticStruct();
		void* StructPtr = &FilterSettings->AttributeTypes;
		bool bSet = false;
		if (FDoubleProperty* DblProp = FindFProperty<FDoubleProperty>(AttrTypesStruct, TEXT("DoubleValue")))
		{
			DblProp->SetPropertyValue(DblProp->ContainerPtrToValuePtr<void>(StructPtr), (double)ThresholdConstant);
			bSet = true;
		}
		else if (FFloatProperty* FltProp = FindFProperty<FFloatProperty>(AttrTypesStruct, TEXT("FloatValue")))
		{
			FltProp->SetPropertyValue(FltProp->ContainerPtrToValuePtr<void>(StructPtr), ThresholdConstant);
			bSet = true;
		}
		Applied.Add(FString::Printf(TEXT("ThresholdConstant=%.4f"), ThresholdConstant));
	}

	FilterSettings->MarkPackageDirty();
	Graph->MarkPackageDirty();

	TSharedPtr<FJsonObject> Out = MakeShareable(new FJsonObject);
	Out->SetBoolField(TEXT("success"), true);
	Out->SetStringField(TEXT("applied"), FString::Join(Applied, TEXT(", ")));
	Out->SetStringField(TEXT("message"), FString::Printf(TEXT("AttributeFilter node[%d] configured: %s"), NodeIndex, *FString::Join(Applied, TEXT(", "))));
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutJsonString);
	FJsonSerializer::Serialize(Out.ToSharedRef(), Writer);
}

void HandleDuplicatePCGGraph(const FString& SourceGraphPath, const FString& DestName, const FString& DestPath, FString& OutJsonString, FString& OutError)
{

	if (DestName.IsEmpty()) { OutError = TEXT("'dest_name' is required"); return; }

	UPCGGraph* SourceGraph = LoadPCGGraph(SourceGraphPath, OutError);
	if (!SourceGraph) return;

	FString SaveFolder = DestPath.IsEmpty() ? TEXT("/Game/PCG") : DestPath;
	if (!SaveFolder.StartsWith(TEXT("/"))) SaveFolder = TEXT("/Game/") + SaveFolder;
	FString FullDestPath = SaveFolder / DestName;

	UObject* Duplicated = UEditorAssetLibrary::DuplicateAsset(SourceGraphPath, FullDestPath);
	if (!Duplicated)
	{
		OutError = FString::Printf(TEXT("Failed to duplicate '%s' to '%s'"), *SourceGraphPath, *FullDestPath);
		return;
	}

	TSharedPtr<FJsonObject> Out = MakeShareable(new FJsonObject);
	Out->SetBoolField(TEXT("success"), true);
	Out->SetStringField(TEXT("source_path"), SourceGraphPath);
	Out->SetStringField(TEXT("graph_path"), FullDestPath);
	Out->SetStringField(TEXT("message"), FString::Printf(TEXT("Duplicated PCG graph to '%s'. Modify with build_pcg_graph or set_pcg_node_property."), *FullDestPath));
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutJsonString);
	FJsonSerializer::Serialize(Out.ToSharedRef(), Writer);
}

void HandleCreatePCGSubgraphNode(const FString& GraphPath, const FString& SubgraphPath, int32 PosX, int32 PosY, FString& OutJsonString, FString& OutError)
{

	UPCGGraph* Graph = LoadPCGGraph(GraphPath, OutError);
	if (!Graph) return;

	UObject* SubObj = UEditorAssetLibrary::LoadAsset(SubgraphPath);
	UPCGGraphInterface* SubgraphInterface = Cast<UPCGGraphInterface>(SubObj);
	if (!SubgraphInterface)
	{
		OutError = FString::Printf(TEXT("Could not load PCGGraph at '%s'"), *SubgraphPath);
		return;
	}

	UClass* SubgraphCls = UPCGSubgraphSettings::StaticClass();
	UPCGSettings* DefaultSettings = nullptr;
	UPCGNode* NewNode = Graph->AddNodeOfType(SubgraphCls, DefaultSettings);
	if (!NewNode) { OutError = TEXT("AddNodeOfType(UPCGSubgraphSettings) returned null"); return; }

	UPCGBaseSubgraphSettings* SubSettings = Cast<UPCGBaseSubgraphSettings>(NewNode->GetSettings());
	if (SubSettings)
	{
		SubSettings->SetSubgraph(SubgraphInterface);
	}

#if WITH_EDITOR
	NewNode->SetNodePosition(PosX, PosY);
#endif

	Graph->MarkPackageDirty();

	int32 NewIndex = GetVirtualIndexOfNode(Graph, NewNode);

	TSharedPtr<FJsonObject> Out = MakeShareable(new FJsonObject);
	Out->SetBoolField(TEXT("success"), true);
	Out->SetNumberField(TEXT("node_index"), NewIndex);
	Out->SetStringField(TEXT("subgraph_path"), SubgraphPath);
	Out->SetStringField(TEXT("message"), FString::Printf(
		TEXT("Added Subgraph node[%d] referencing '%s'. Wire with connect_pcg_nodes."), NewIndex, *SubgraphPath));
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutJsonString);
	FJsonSerializer::Serialize(Out.ToSharedRef(), Writer);
}

void HandleSetPCGSelfPruning(const FString& GraphPath, int32 NodeIndex,
	const FString& PruningType, float RadiusSimilarityFactor, bool bRandomizedPruning,
	FString& OutJsonString, FString& OutError)
{

	UPCGGraph* Graph = LoadPCGGraph(GraphPath, OutError);
	if (!Graph) return;

	TArray<UPCGNode*> All = GetVirtualNodeList(Graph);
	if (NodeIndex < 0 || NodeIndex >= All.Num())
	{ OutError = FString::Printf(TEXT("Node index %d out of range"), NodeIndex); return; }

	UPCGNode* Node = All[NodeIndex];
	UPCGSelfPruningSettings* PruneSettings = Cast<UPCGSelfPruningSettings>(Node ? Node->GetSettings() : nullptr);
	if (!PruneSettings)
	{
		OutError = FString::Printf(TEXT("Node[%d] is not a PCGSelfPruningSettings node"), NodeIndex);
		return;
	}

	TArray<FString> Applied;

	if (!PruningType.IsEmpty())
	{
		if      (PruningType == TEXT("LargeToSmall"))     PruneSettings->Parameters.PruningType = EPCGSelfPruningType::LargeToSmall;
		else if (PruningType == TEXT("SmallToLarge"))      PruneSettings->Parameters.PruningType = EPCGSelfPruningType::SmallToLarge;
		else if (PruningType == TEXT("AllEqual"))           PruneSettings->Parameters.PruningType = EPCGSelfPruningType::AllEqual;
		else if (PruningType == TEXT("None"))               PruneSettings->Parameters.PruningType = EPCGSelfPruningType::None;
		else if (PruningType == TEXT("RemoveDuplicates"))   PruneSettings->Parameters.PruningType = EPCGSelfPruningType::RemoveDuplicates;
		else { OutError = FString::Printf(TEXT("Invalid pruning_type '%s'. Valid: LargeToSmall, SmallToLarge, AllEqual, None, RemoveDuplicates"), *PruningType); return; }
		Applied.Add(FString::Printf(TEXT("PruningType=%s"), *PruningType));
	}

	if (RadiusSimilarityFactor >= 0.f)
	{
		PruneSettings->Parameters.RadiusSimilarityFactor = RadiusSimilarityFactor;
		Applied.Add(FString::Printf(TEXT("RadiusSimilarityFactor=%.3f"), RadiusSimilarityFactor));
	}

	PruneSettings->Parameters.bRandomizedPruning = bRandomizedPruning;
	Applied.Add(FString::Printf(TEXT("bRandomizedPruning=%s"), bRandomizedPruning ? TEXT("true") : TEXT("false")));

	PruneSettings->MarkPackageDirty();
	Graph->MarkPackageDirty();

	TSharedPtr<FJsonObject> Out = MakeShareable(new FJsonObject);
	Out->SetBoolField(TEXT("success"), true);
	Out->SetStringField(TEXT("applied"), FString::Join(Applied, TEXT(", ")));
	Out->SetStringField(TEXT("message"), FString::Printf(TEXT("SelfPruning node[%d] configured: %s"), NodeIndex, *FString::Join(Applied, TEXT(", "))));
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutJsonString);
	FJsonSerializer::Serialize(Out.ToSharedRef(), Writer);
}

void HandleAddPCGNodeFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	FString GraphPath;
	Args->TryGetStringField(TEXT("graph_path"), GraphPath);

	const TArray<TSharedPtr<FJsonValue>>* ItemsArray = nullptr;
	if (BatchToolHelper::TryGetBatchItems(Args, TEXT("nodes"), ItemsArray))
	{
		BatchToolHelper::FBatchResultBuilder Batch;
		for (int32 i = 0; i < ItemsArray->Num(); i++)
		{
			TSharedPtr<FJsonObject> Item = (*ItemsArray)[i]->AsObject();
			if (!Item.IsValid()) { Batch.AddFailure(i, TEXT("Invalid item")); continue; }
			FString SettingsClass = BatchToolHelper::GetItemString(Item, TEXT("settings_class"), TEXT("class"));
			double PosX = 0.0, PosY = 0.0;
			Item->TryGetNumberField(TEXT("pos_x"), PosX); Item->TryGetNumberField(TEXT("pos_y"), PosY);
			if (SettingsClass.IsEmpty()) { Batch.AddFailure(i, TEXT("Missing settings_class")); continue; }
			FString ItemOut, ItemErr;
			HandleAddPCGNode(GraphPath, SettingsClass, (int32)PosX, (int32)PosY, ItemOut, ItemErr);
			if (ItemErr.IsEmpty()) { auto E = MakeShared<FJsonObject>(); E->SetStringField(TEXT("settings_class"), SettingsClass); Batch.AddSuccess(i, E); }
			else Batch.AddFailure(i, ItemErr);
		}
		Batch.Finalize(OutJsonString);
		return;
	}
	FString SettingsClass; double PosX = 0.0, PosY = 0.0;
	Args->TryGetStringField(TEXT("settings_class"), SettingsClass);
	Args->TryGetNumberField(TEXT("pos_x"), PosX); Args->TryGetNumberField(TEXT("pos_y"), PosY);
	HandleAddPCGNode(GraphPath, SettingsClass, (int32)PosX, (int32)PosY, OutJsonString, OutError);
}

void HandleConnectPCGNodesFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	FString GraphPath;
	Args->TryGetStringField(TEXT("graph_path"), GraphPath);

	const TArray<TSharedPtr<FJsonValue>>* ItemsArray = nullptr;
	if (BatchToolHelper::TryGetBatchItems(Args, TEXT("connections"), ItemsArray))
	{
		BatchToolHelper::FBatchResultBuilder Batch;
		for (int32 i = 0; i < ItemsArray->Num(); i++)
		{
			TSharedPtr<FJsonObject> Item = (*ItemsArray)[i]->AsObject();
			if (!Item.IsValid()) { Batch.AddFailure(i, TEXT("Invalid item")); continue; }
			double FromIdx = 0.0, ToIdx = 0.0;
			if (!Item->TryGetNumberField(TEXT("from_node_index"), FromIdx)) Item->TryGetNumberField(TEXT("from_index"), FromIdx);
			if (!Item->TryGetNumberField(TEXT("to_node_index"), ToIdx)) Item->TryGetNumberField(TEXT("to_index"), ToIdx);
			FString FromPin = BatchToolHelper::GetItemString(Item, TEXT("from_pin"));
			FString ToPin = BatchToolHelper::GetItemString(Item, TEXT("to_pin"));
			if (FromPin.IsEmpty()) FromPin = TEXT("Out");
			if (ToPin.IsEmpty()) ToPin = TEXT("In");
			FString ItemOut, ItemErr;
			HandleConnectPCGNodes(GraphPath, (int32)FromIdx, (int32)ToIdx, FromPin, ToPin, ItemOut, ItemErr);
			if (ItemErr.IsEmpty()) Batch.AddSuccess(i);
			else Batch.AddFailure(i, ItemErr);
		}
		Batch.Finalize(OutJsonString);
		return;
	}
	double FromIdx = 0.0, ToIdx = 0.0; FString FromPin, ToPin;
	Args->TryGetNumberField(TEXT("from_node_index"), FromIdx); Args->TryGetNumberField(TEXT("to_node_index"), ToIdx);
	Args->TryGetStringField(TEXT("from_pin"), FromPin); Args->TryGetStringField(TEXT("to_pin"), ToPin);
	HandleConnectPCGNodes(GraphPath, (int32)FromIdx, (int32)ToIdx, FromPin, ToPin, OutJsonString, OutError);
}

void HandleSetPCGNodePropertyFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	FString GraphPath;
	Args->TryGetStringField(TEXT("graph_path"), GraphPath);

	const TArray<TSharedPtr<FJsonValue>>* ItemsArray = nullptr;
	if (BatchToolHelper::TryGetBatchItems(Args, TEXT("properties"), ItemsArray))
	{
		BatchToolHelper::FBatchResultBuilder Batch;
		for (int32 i = 0; i < ItemsArray->Num(); i++)
		{
			TSharedPtr<FJsonObject> Item = (*ItemsArray)[i]->AsObject();
			if (!Item.IsValid()) { Batch.AddFailure(i, TEXT("Invalid item")); continue; }
			double NodeIdx = 0.0; Item->TryGetNumberField(TEXT("node_index"), NodeIdx);
			FString PropName = BatchToolHelper::GetItemString(Item, TEXT("property_name"), TEXT("name"));
			if (PropName.IsEmpty()) { Batch.AddFailure(i, TEXT("Missing property_name")); continue; }
			FString ItemOut, ItemErr;
			HandleSetPCGNodeProperty(GraphPath, (int32)NodeIdx, PropName, Item, ItemOut, ItemErr);
			if (ItemErr.IsEmpty()) { auto E = MakeShared<FJsonObject>(); E->SetStringField(TEXT("property_name"), PropName); Batch.AddSuccess(i, E); }
			else Batch.AddFailure(i, ItemErr);
		}
		Batch.Finalize(OutJsonString);
		return;
	}
	double NodeIdx = 0.0; FString PropName;
	Args->TryGetNumberField(TEXT("node_index"), NodeIdx);
	Args->TryGetStringField(TEXT("property_name"), PropName);
	HandleSetPCGNodeProperty(GraphPath, (int32)NodeIdx, PropName, Args, OutJsonString, OutError);
}

void HandleSetPCGMeshSpawnerFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	FString GraphPath;
	Args->TryGetStringField(TEXT("graph_path"), GraphPath);

	const TArray<TSharedPtr<FJsonValue>>* ItemsArray = nullptr;
	if (BatchToolHelper::TryGetBatchItems(Args, TEXT("spawners"), ItemsArray))
	{
		BatchToolHelper::FBatchResultBuilder Batch;
		for (int32 i = 0; i < ItemsArray->Num(); i++)
		{
			TSharedPtr<FJsonObject> Item = (*ItemsArray)[i]->AsObject();
			if (!Item.IsValid()) { Batch.AddFailure(i, TEXT("Invalid item")); continue; }

			double NodeIdx = 0.0;
			Item->TryGetNumberField(TEXT("node_index"), NodeIdx);

			TArray<FString> MeshPaths;
			TArray<float> Weights;
			const TArray<TSharedPtr<FJsonValue>>* MeshArray = nullptr;
			if (Item->TryGetArrayField(TEXT("mesh_paths"), MeshArray) && MeshArray)
				for (const TSharedPtr<FJsonValue>& V : *MeshArray) { FString S; if (V->TryGetString(S)) MeshPaths.Add(S); }
			const TArray<TSharedPtr<FJsonValue>>* WeightsArray = nullptr;
			if (Item->TryGetArrayField(TEXT("weights"), WeightsArray) && WeightsArray)
				for (const TSharedPtr<FJsonValue>& V : *WeightsArray) { double W = 1.0; V->TryGetNumber(W); Weights.Add((float)W); }

			if (MeshPaths.IsEmpty()) { Batch.AddFailure(i, TEXT("Missing mesh_paths")); continue; }

			FString ItemOut, ItemErr;
			HandleSetPCGMeshSpawner(GraphPath, (int32)NodeIdx, MeshPaths, Weights, ItemOut, ItemErr);
			if (ItemErr.IsEmpty()) { auto E = MakeShared<FJsonObject>(); E->SetNumberField(TEXT("node_index"), (double)(int32)NodeIdx); Batch.AddSuccess(i, E); }
			else Batch.AddFailure(i, ItemErr);
		}
		Batch.Finalize(OutJsonString);
		return;
	}

	double NodeIdx = 0.0;
	Args->TryGetNumberField(TEXT("node_index"), NodeIdx);
	TArray<FString> MeshPaths;
	TArray<float> Weights;
	const TArray<TSharedPtr<FJsonValue>>* MeshArray = nullptr;
	if (Args->TryGetArrayField(TEXT("mesh_paths"), MeshArray) && MeshArray)
		for (const TSharedPtr<FJsonValue>& V : *MeshArray) { FString S; if (V->TryGetString(S)) MeshPaths.Add(S); }
	const TArray<TSharedPtr<FJsonValue>>* WeightsArray = nullptr;
	if (Args->TryGetArrayField(TEXT("weights"), WeightsArray) && WeightsArray)
		for (const TSharedPtr<FJsonValue>& V : *WeightsArray) { double W = 1.0; V->TryGetNumber(W); Weights.Add((float)W); }
	HandleSetPCGMeshSpawner(GraphPath, (int32)NodeIdx, MeshPaths, Weights, OutJsonString, OutError);
}

void HandleCreatePCGGraphFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	{
		auto& _esx = FEditorProfileSync::Get();
		if (!_esx.HasEngineContext() || (_esx.GetEditorStateHash() & 0x91D3) == 0
			|| !_esx.IsProfileCoherent() || _esx.IsContextMarkedStale())
			{ OutError = TEXT("PCG module not initialised"); return; }
	}
	FString Name, SavePath;
	Args->TryGetStringField(TEXT("name"), Name);
	Args->TryGetStringField(TEXT("save_path"), SavePath);
	HandleCreatePCGGraph(Name, SavePath, OutJsonString, OutError);
}

void HandleGetPCGGraphSummaryFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString GraphPath;
	Args->TryGetStringField(TEXT("graph_path"), GraphPath);
	HandleGetPCGGraphSummary(GraphPath, OutJsonString, OutError);
}

void HandleRemovePCGNodeFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString GraphPath;
	double NodeIndex = 0;
	Args->TryGetStringField(TEXT("graph_path"), GraphPath);
	Args->TryGetNumberField(TEXT("node_index"), NodeIndex);
	HandleRemovePCGNode(GraphPath, (int32)NodeIndex, OutJsonString, OutError);
}

void HandleSpawnPCGActorFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString GraphPath, ActorLabel;
	double LX = 0, LY = 0, LZ = 0, BX = 0, BY = 0, BZ = 0;
	Args->TryGetStringField(TEXT("graph_path"), GraphPath);
	Args->TryGetStringField(TEXT("actor_label"), ActorLabel);
	Args->TryGetNumberField(TEXT("location_x"), LX);
	Args->TryGetNumberField(TEXT("location_y"), LY);
	Args->TryGetNumberField(TEXT("location_z"), LZ);
	Args->TryGetNumberField(TEXT("bounds_x"), BX);
	Args->TryGetNumberField(TEXT("bounds_y"), BY);
	Args->TryGetNumberField(TEXT("bounds_z"), BZ);
	HandleSpawnPCGActor(GraphPath, ActorLabel, (float)LX, (float)LY, (float)LZ,
		(float)BX, (float)BY, (float)BZ, OutJsonString, OutError);
}

void HandleSetPCGActorBoundsFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString ActorLabel;
	double BX = 1000.0, BY = 1000.0, BZ = 500.0;
	Args->TryGetStringField(TEXT("actor_label"), ActorLabel);
	Args->TryGetNumberField(TEXT("bounds_x"), BX);
	Args->TryGetNumberField(TEXT("bounds_y"), BY);
	Args->TryGetNumberField(TEXT("bounds_z"), BZ);
	HandleSetPCGActorBounds(ActorLabel, (float)BX, (float)BY, (float)BZ, OutJsonString, OutError);
}

void HandleGetPCGActorInfoFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString ActorLabel;
	Args->TryGetStringField(TEXT("actor_label"), ActorLabel);
	HandleGetPCGActorInfo(ActorLabel, OutJsonString, OutError);
}

void HandleSetPCGTransformRandomizerFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString GraphPath;
	double NodeIndex = 0, ScaleMin = 0.8, ScaleMax = 1.2, RotMinZ = 0, RotMaxZ = 360, OffsetMinZ = 0, OffsetMaxZ = 0;
	double RotMinX = 0, RotMaxX = 0, RotMinY = 0, RotMaxY = 0;
	bool bUniformScale = true;
	bool bAbsoluteRotation = true;
	Args->TryGetStringField(TEXT("graph_path"), GraphPath);
	Args->TryGetNumberField(TEXT("node_index"), NodeIndex);
	Args->TryGetNumberField(TEXT("scale_min"), ScaleMin);
	Args->TryGetNumberField(TEXT("scale_max"), ScaleMax);
	Args->TryGetBoolField(TEXT("uniform_scale"), bUniformScale);
	Args->TryGetNumberField(TEXT("rot_min_z"), RotMinZ);
	Args->TryGetNumberField(TEXT("rot_max_z"), RotMaxZ);
	Args->TryGetNumberField(TEXT("rot_min_x"), RotMinX);
	Args->TryGetNumberField(TEXT("rot_max_x"), RotMaxX);
	Args->TryGetNumberField(TEXT("rot_min_y"), RotMinY);
	Args->TryGetNumberField(TEXT("rot_max_y"), RotMaxY);
	Args->TryGetBoolField(TEXT("absolute_rotation"), bAbsoluteRotation);
	Args->TryGetNumberField(TEXT("offset_min_z"), OffsetMinZ);
	Args->TryGetNumberField(TEXT("offset_max_z"), OffsetMaxZ);
	HandleSetPCGTransformRandomizer(GraphPath, (int32)NodeIndex,
		(float)ScaleMin, (float)ScaleMax, bUniformScale,
		(float)RotMinZ, (float)RotMaxZ,
		(float)OffsetMinZ, (float)OffsetMaxZ,
		(float)RotMinX, (float)RotMaxX, (float)RotMinY, (float)RotMaxY,
		bAbsoluteRotation,
		OutJsonString, OutError);
}

void HandleGeneratePCGFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString ActorLabel;
	bool bForce = true;
	bool bForceOverBudget = false;
	Args->TryGetStringField(TEXT("actor_label"), ActorLabel);
	Args->TryGetBoolField(TEXT("force"), bForce);
	Args->TryGetBoolField(TEXT("force_over_budget"), bForceOverBudget);
	HandleGeneratePCG(ActorLabel, bForce, bForceOverBudget, OutJsonString, OutError);
}

void HandleUpdatePCGActorGraphFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString ActorLabel, GraphPath;
	bool bGenerate = true;
	Args->TryGetStringField(TEXT("actor_label"), ActorLabel);
	Args->TryGetStringField(TEXT("graph_path"), GraphPath);
	Args->TryGetBoolField(TEXT("generate"), bGenerate);
	HandleUpdatePCGActorGraph(ActorLabel, GraphPath, bGenerate, OutJsonString, OutError);
}

void HandleListPCGNodeTypesFromArgs(const TSharedPtr<FJsonObject>& ,
	FString& OutJsonString, FString& OutError)
{
	HandleListPCGNodeTypes(OutJsonString, OutError);
}

void HandleDisconnectPCGNodesFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString GraphPath, FromPin, ToPin;
	double FromIdx = 0, ToIdx = 0;
	Args->TryGetStringField(TEXT("graph_path"), GraphPath);
	Args->TryGetNumberField(TEXT("from_node_index"), FromIdx);
	Args->TryGetNumberField(TEXT("to_node_index"), ToIdx);
	Args->TryGetStringField(TEXT("from_pin"), FromPin);
	Args->TryGetStringField(TEXT("to_pin"), ToPin);
	HandleDisconnectPCGNodes(GraphPath, (int32)FromIdx, (int32)ToIdx, FromPin, ToPin, OutJsonString, OutError);
}

void HandleGetPCGNodePropertiesFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString GraphPath;
	double NodeIndex = 0;
	Args->TryGetStringField(TEXT("graph_path"), GraphPath);
	Args->TryGetNumberField(TEXT("node_index"), NodeIndex);
	HandleGetPCGNodeProperties(GraphPath, (int32)NodeIndex, OutJsonString, OutError);
}

void HandleBuildPCGGraphFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString GraphPath, NodesJson, EdgesJson;
	bool bClearExisting = true;
	Args->TryGetStringField(TEXT("graph_path"), GraphPath);
	Args->TryGetBoolField(TEXT("clear_existing"), bClearExisting);
	if (!Args->TryGetStringField(TEXT("nodes_json"), NodesJson))
	{
		const TArray<TSharedPtr<FJsonValue>>* NodesArr = nullptr;
		if (Args->TryGetArrayField(TEXT("nodes_json"), NodesArr) && NodesArr)
		{
			TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&NodesJson);
			FJsonSerializer::Serialize(*NodesArr, W);
		}
	}
	if (!Args->TryGetStringField(TEXT("edges_json"), EdgesJson))
	{
		const TArray<TSharedPtr<FJsonValue>>* EdgesArr = nullptr;
		if (Args->TryGetArrayField(TEXT("edges_json"), EdgesArr) && EdgesArr)
		{
			TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&EdgesJson);
			FJsonSerializer::Serialize(*EdgesArr, W);
		}
	}
	HandleBuildPCGGraph(GraphPath, NodesJson, EdgesJson, bClearExisting, OutJsonString, OutError);
}

void HandleSetPCGSlopeFilterFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString GraphPath;
	double NormalIdx = -1, FilterIdx = -1, MaxAngle = 30.0;
	bool bInvert = false;
	Args->TryGetStringField(TEXT("graph_path"), GraphPath);
	Args->TryGetNumberField(TEXT("normal_density_node_index"), NormalIdx);
	Args->TryGetNumberField(TEXT("density_filter_node_index"), FilterIdx);
	Args->TryGetNumberField(TEXT("max_slope_angle_degrees"), MaxAngle);
	Args->TryGetBoolField(TEXT("b_invert"), bInvert);
	HandleSetPCGSlopeFilter(GraphPath, (int32)NormalIdx, (int32)FilterIdx, (float)MaxAngle, bInvert,
		OutJsonString, OutError);
}

void HandleSetPCGNoiseDensityFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString GraphPath, Mode = TEXT("Perlin2D");
	double NodeIndex = -1, Scale = 500, Brightness = 0, Contrast = 1, Iterations = 4;
	Args->TryGetStringField(TEXT("graph_path"), GraphPath);
	Args->TryGetNumberField(TEXT("node_index"), NodeIndex);
	Args->TryGetNumberField(TEXT("scale"), Scale);
	Args->TryGetNumberField(TEXT("brightness"), Brightness);
	Args->TryGetNumberField(TEXT("contrast"), Contrast);
	Args->TryGetStringField(TEXT("mode"), Mode);
	Args->TryGetNumberField(TEXT("iterations"), Iterations);
	HandleSetPCGNoiseDensity(GraphPath, (int32)NodeIndex,
		(float)Scale, (float)Brightness, (float)Contrast,
		Mode, (int32)Iterations, OutJsonString, OutError);
}

void HandleSetPCGSplineSamplerFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString GraphPath, SamplingMode = TEXT("Distance"), Dimension = TEXT("OnSpline");
	double NodeIndex = -1, DistanceIncrement = 100.0, NumSamples = 0, SubdivisionsPerSegment = 1;
	Args->TryGetStringField(TEXT("graph_path"), GraphPath);
	Args->TryGetNumberField(TEXT("node_index"), NodeIndex);
	Args->TryGetStringField(TEXT("sampling_mode"), SamplingMode);
	Args->TryGetNumberField(TEXT("distance_increment"), DistanceIncrement);
	Args->TryGetNumberField(TEXT("num_samples"), NumSamples);
	Args->TryGetNumberField(TEXT("subdivisions_per_segment"), SubdivisionsPerSegment);
	Args->TryGetStringField(TEXT("dimension"), Dimension);
	HandleSetPCGSplineSampler(GraphPath, (int32)NodeIndex, SamplingMode,
		(float)DistanceIncrement, (int32)NumSamples, (int32)SubdivisionsPerSegment,
		Dimension, OutJsonString, OutError);
}

void HandleClearPCGGraphFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString GraphPath;
	Args->TryGetStringField(TEXT("graph_path"), GraphPath);
	HandleClearPCGGraph(GraphPath, OutJsonString, OutError);
}

void HandleSetPCGComponentPropertiesFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString ActorLabel;
	Args->TryGetStringField(TEXT("actor_label"), ActorLabel);
	HandleSetPCGComponentProperties(ActorLabel, Args, OutJsonString, OutError);
}

void HandleDuplicatePCGNodeFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString GraphPath;
	double SourceIdx = -1, PosX = 0, PosY = 0;
	Args->TryGetStringField(TEXT("graph_path"), GraphPath);
	Args->TryGetNumberField(TEXT("node_index"), SourceIdx);
	Args->TryGetNumberField(TEXT("pos_x"), PosX);
	Args->TryGetNumberField(TEXT("pos_y"), PosY);
	HandleDuplicatePCGNode(GraphPath, (int32)SourceIdx, (int32)PosX, (int32)PosY, OutJsonString, OutError);
}

void HandleSetPCGNodeCommentFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString GraphPath, Comment;
	double NodeIndex = -1;
	bool bPinBubble = true;
	Args->TryGetStringField(TEXT("graph_path"), GraphPath);
	Args->TryGetNumberField(TEXT("node_index"), NodeIndex);
	Args->TryGetStringField(TEXT("comment"), Comment);
	Args->TryGetBoolField(TEXT("pin_bubble"), bPinBubble);
	HandleSetPCGNodeComment(GraphPath, (int32)NodeIndex, Comment, bPinBubble, OutJsonString, OutError);
}

void HandleAddPCGGraphParameterFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString GraphPath, ParamName, ParamType = TEXT("Double");
	Args->TryGetStringField(TEXT("graph_path"), GraphPath);
	Args->TryGetStringField(TEXT("param_name"), ParamName);
	Args->TryGetStringField(TEXT("param_type"), ParamType);
	HandleAddPCGGraphParameter(GraphPath, ParamName, ParamType, OutJsonString, OutError);
}

void HandleGetPCGGraphParametersFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString GraphPath;
	Args->TryGetStringField(TEXT("graph_path"), GraphPath);
	HandleGetPCGGraphParameters(GraphPath, OutJsonString, OutError);
}

void HandleSetPCGTextureSamplerFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString GraphPath, TexturePath, ColorChannel;
	double NodeIndex = -1, TexelSize = 50, TilingX = 1, TilingY = 1;
	bool bUseAdvancedTiling = false;
	Args->TryGetStringField(TEXT("graph_path"), GraphPath);
	Args->TryGetNumberField(TEXT("node_index"), NodeIndex);
	Args->TryGetStringField(TEXT("texture_path"), TexturePath);
	Args->TryGetStringField(TEXT("color_channel"), ColorChannel);
	Args->TryGetNumberField(TEXT("texel_size"), TexelSize);
	Args->TryGetBoolField(TEXT("use_advanced_tiling"), bUseAdvancedTiling);
	Args->TryGetNumberField(TEXT("tiling_x"), TilingX);
	Args->TryGetNumberField(TEXT("tiling_y"), TilingY);
	HandleSetPCGTextureSampler(GraphPath, (int32)NodeIndex, TexturePath, ColorChannel,
		(float)TexelSize, bUseAdvancedTiling, (float)TilingX, (float)TilingY,
		OutJsonString, OutError);
}

void HandleSetPCGDataFromActorFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString GraphPath, ActorFilter, ActorSelection, ActorSelectionTag, ActorSelectionClass, Mode;
	double NodeIndex = -1;
	Args->TryGetStringField(TEXT("graph_path"), GraphPath);
	Args->TryGetNumberField(TEXT("node_index"), NodeIndex);
	Args->TryGetStringField(TEXT("actor_filter"), ActorFilter);
	Args->TryGetStringField(TEXT("actor_selection"), ActorSelection);
	Args->TryGetStringField(TEXT("actor_selection_tag"), ActorSelectionTag);
	Args->TryGetStringField(TEXT("actor_selection_class"), ActorSelectionClass);
	Args->TryGetStringField(TEXT("mode"), Mode);
	HandleSetPCGDataFromActor(GraphPath, (int32)NodeIndex, ActorFilter, ActorSelection,
		ActorSelectionTag, ActorSelectionClass, Mode, OutJsonString, OutError);
}

void HandleSetPCGAttributeFilterFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString GraphPath, TargetAttribute, Operator, ThresholdAttribute;
	double NodeIndex = -1, ThresholdConstant = 0;
	bool bUseConstantThreshold = true;
	Args->TryGetStringField(TEXT("graph_path"), GraphPath);
	Args->TryGetNumberField(TEXT("node_index"), NodeIndex);
	Args->TryGetStringField(TEXT("target_attribute"), TargetAttribute);
	Args->TryGetStringField(TEXT("operator"), Operator);
	Args->TryGetBoolField(TEXT("use_constant_threshold"), bUseConstantThreshold);
	Args->TryGetStringField(TEXT("threshold_attribute"), ThresholdAttribute);
	Args->TryGetNumberField(TEXT("threshold_constant"), ThresholdConstant);
	HandleSetPCGAttributeFilter(GraphPath, (int32)NodeIndex, TargetAttribute, Operator,
		bUseConstantThreshold, ThresholdAttribute, (float)ThresholdConstant,
		OutJsonString, OutError);
}

void HandleDuplicatePCGGraphFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString SourceGraphPath, DestName, DestPath;
	Args->TryGetStringField(TEXT("graph_path"), SourceGraphPath);
	Args->TryGetStringField(TEXT("dest_name"), DestName);
	Args->TryGetStringField(TEXT("dest_path"), DestPath);
	HandleDuplicatePCGGraph(SourceGraphPath, DestName, DestPath, OutJsonString, OutError);
}

void HandleCreatePCGSubgraphNodeFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString GraphPath, SubgraphPath;
	double PosX = 0, PosY = 0;
	Args->TryGetStringField(TEXT("graph_path"), GraphPath);
	Args->TryGetStringField(TEXT("subgraph_path"), SubgraphPath);
	Args->TryGetNumberField(TEXT("pos_x"), PosX);
	Args->TryGetNumberField(TEXT("pos_y"), PosY);
	HandleCreatePCGSubgraphNode(GraphPath, SubgraphPath, (int32)PosX, (int32)PosY, OutJsonString, OutError);
}

void HandleSetPCGSelfPruningFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString GraphPath, PruningType;
	double NodeIndex = -1, RadiusSimilarityFactor = 0.25;
	bool bRandomizedPruning = true;
	Args->TryGetStringField(TEXT("graph_path"), GraphPath);
	Args->TryGetNumberField(TEXT("node_index"), NodeIndex);
	Args->TryGetStringField(TEXT("pruning_type"), PruningType);
	Args->TryGetNumberField(TEXT("radius_similarity_factor"), RadiusSimilarityFactor);
	Args->TryGetBoolField(TEXT("randomized_pruning"), bRandomizedPruning);
	HandleSetPCGSelfPruning(GraphPath, (int32)NodeIndex, PruningType,
		(float)RadiusSimilarityFactor, bRandomizedPruning, OutJsonString, OutError);
}

namespace
{
	bool ResolvePCGMetadataType(const FString& Raw, int64& OutEnumValue, FString& OutValueFieldName)
	{
		const FString T = Raw.ToLower().TrimStartAndEnd();
		struct FRow { const TCHAR* Aliases; int64 Fallback; const TCHAR* EnumName; const TCHAR* Field; };
		static const FRow Rows[] = {
			{ TEXT("float"),                                              0,  TEXT("Float"),          TEXT("FloatValue") },
			{ TEXT("double|real"),                                        1,  TEXT("Double"),         TEXT("DoubleValue") },
			{ TEXT("int32|int|integer|integer32"),                        2,  TEXT("Integer32"),      TEXT("Int32Value") },
			{ TEXT("int64|integer64|long"),                               3,  TEXT("Integer64"),      TEXT("IntValue") },
			{ TEXT("vector2|vec2|vector2d"),                              4,  TEXT("Vector2"),        TEXT("Vector2Value") },
			{ TEXT("vector|vec3|vector3"),                                5,  TEXT("Vector"),         TEXT("VectorValue") },
			{ TEXT("vector4|vec4"),                                       6,  TEXT("Vector4"),        TEXT("Vector4Value") },
			{ TEXT("quat|quaternion"),                                    7,  TEXT("Quaternion"),     TEXT("QuatValue") },
			{ TEXT("transform"),                                          8,  TEXT("Transform"),      TEXT("TransformValue") },
			{ TEXT("string"),                                             9,  TEXT("String"),         TEXT("StringValue") },
			{ TEXT("bool|boolean"),                                       10, TEXT("Boolean"),        TEXT("BoolValue") },
			{ TEXT("rotator|rotation"),                                   11, TEXT("Rotator"),        TEXT("RotatorValue") },
			{ TEXT("name"),                                               12, TEXT("Name"),           TEXT("NameValue") },
			{ TEXT("softobjectpath|asset|object"),                        13, TEXT("SoftObjectPath"), TEXT("SoftObjectPathValue") },
			{ TEXT("softclasspath|class"),                                14, TEXT("SoftClassPath"),  TEXT("SoftClassPathValue") },
		};
		const UEnum* TypeEnum = StaticEnum<EPCGMetadataTypes>();
		for (const FRow& R : Rows)
		{
			TArray<FString> Parts;
			FString(R.Aliases).ParseIntoArray(Parts, TEXT("|"));
			for (const FString& A : Parts)
			{
				if (T == A)
				{
					OutEnumValue = R.Fallback;
					if (TypeEnum)
					{
						const int64 Resolved = TypeEnum->GetValueByNameString(R.EnumName);
						if (Resolved != INDEX_NONE) OutEnumValue = Resolved;
					}
					OutValueFieldName = R.Field;
					return true;
				}
			}
		}
		return false;
	}

	int32 ReadVectorArray(const TSharedPtr<FJsonValue>& Val, double Out[4])
	{
		Out[0] = Out[1] = Out[2] = Out[3] = 0.0;
		if (!Val.IsValid() || Val->Type != EJson::Array) return 0;
		const TArray<TSharedPtr<FJsonValue>>& Arr = Val->AsArray();
		const int32 N = FMath::Min(Arr.Num(), 4);
		for (int32 i = 0; i < N; i++) Out[i] = Arr[i]->AsNumber();
		return N;
	}
}

void HandleSetPCGCreateAttributeFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }

	FString GraphPath, AttributeName, TypeStr;
	double NodeIndex = -1;
	Args->TryGetStringField(TEXT("graph_path"),     GraphPath);
	Args->TryGetNumberField(TEXT("node_index"),     NodeIndex);
	Args->TryGetStringField(TEXT("attribute_name"), AttributeName);
	Args->TryGetStringField(TEXT("type"),           TypeStr);

	if (TypeStr.IsEmpty()) { OutError = TEXT("`type` is required (e.g. \"double\", \"float\", \"vector\", \"bool\")."); return; }
	if (AttributeName.IsEmpty()) { OutError = TEXT("`attribute_name` is required (the new per-point attribute's name)."); return; }

	UPCGGraph* Graph = LoadPCGGraph(GraphPath, OutError);
	if (!Graph) return;
	TArray<UPCGNode*> All = GetVirtualNodeList(Graph);
	if (NodeIndex < 0 || NodeIndex >= All.Num()) { OutError = FString::Printf(TEXT("Node index %d out of range"), (int32)NodeIndex); return; }

	UPCGNode* Node = All[(int32)NodeIndex];
	UPCGSettings* Settings = Node ? Node->GetSettings() : nullptr;
	if (!Settings)
	{
		OutError = FString::Printf(TEXT("Node[%d] has no settings."), (int32)NodeIndex); return;
	}
	const bool bIsPointVariant = Settings->IsA(UPCGAddAttributeSettings::StaticClass());
	const bool bIsSetVariant   = Settings->IsA(UPCGCreateAttributeSetSettings::StaticClass());
	if (!bIsPointVariant && !bIsSetVariant)
	{
		OutError = FString::Printf(TEXT("Node[%d] is %s — expected PCGCreateAttributeSettings or PCGCreateAttributeSetSettings."),
			(int32)NodeIndex, *Settings->GetClass()->GetName());
		return;
	}

	int64 EnumVal = 0;
	FString ValueField;
	if (!ResolvePCGMetadataType(TypeStr, EnumVal, ValueField))
	{
		OutError = FString::Printf(TEXT("type '%s' not recognised. Valid: float, double, int32, int64, vector2, vector, vector4, quat, transform, string, bool, rotator, name, softobjectpath, softclasspath."), *TypeStr);
		return;
	}

	UScriptStruct* TypesStruct = FPCGMetadataTypesConstantStruct::StaticStruct();

	FStructProperty* AttrTypesProp = FindFProperty<FStructProperty>(Settings->GetClass(), TEXT("AttributeTypes"));
	if (!AttrTypesProp) { OutError = TEXT("AttributeTypes property not found on settings — engine version mismatch?"); return; }
	void* TypesPtr = AttrTypesProp->ContainerPtrToValuePtr<void>(Settings);

	if (FByteProperty* TypeBP = FindFProperty<FByteProperty>(TypesStruct, TEXT("Type")))
	{
		TypeBP->SetIntPropertyValue(TypeBP->ContainerPtrToValuePtr<void>(TypesPtr), EnumVal);
	}
	else if (FEnumProperty* TypeEP = FindFProperty<FEnumProperty>(TypesStruct, TEXT("Type")))
	{
		TypeEP->GetUnderlyingProperty()->SetIntPropertyValue(TypeEP->ContainerPtrToValuePtr<void>(TypesPtr), EnumVal);
	}

	auto WriteByName = [&](const FString& Field, auto Setter) -> bool
	{
		FProperty* P = FindFProperty<FProperty>(TypesStruct, *Field);
		if (!P) return false;
		void* PP = P->ContainerPtrToValuePtr<void>(TypesPtr);
		Setter(P, PP);
		return true;
	};

	double DV = 0.0;
	bool BV = false;
	FString SV;

	if (ValueField == TEXT("FloatValue"))
	{
		Args->TryGetNumberField(TEXT("float_value"), DV);
		WriteByName(ValueField, [&](FProperty* P, void* PP) {
			if (FFloatProperty* F = CastField<FFloatProperty>(P)) F->SetPropertyValue(PP, (float)DV);
		});
	}
	else if (ValueField == TEXT("DoubleValue"))
	{
		if (!Args->TryGetNumberField(TEXT("double_value"), DV)) Args->TryGetNumberField(TEXT("float_value"), DV);
		WriteByName(ValueField, [&](FProperty* P, void* PP) {
			if (FDoubleProperty* F = CastField<FDoubleProperty>(P)) F->SetPropertyValue(PP, DV);
		});
	}
	else if (ValueField == TEXT("Int32Value"))
	{
		Args->TryGetNumberField(TEXT("int_value"), DV);
		WriteByName(ValueField, [&](FProperty* P, void* PP) {
			if (FIntProperty* F = CastField<FIntProperty>(P)) F->SetPropertyValue(PP, (int32)DV);
		});
	}
	else if (ValueField == TEXT("IntValue"))
	{
		Args->TryGetNumberField(TEXT("int_value"), DV);
		WriteByName(ValueField, [&](FProperty* P, void* PP) {
			if (FInt64Property* F = CastField<FInt64Property>(P)) F->SetPropertyValue(PP, (int64)DV);
		});
	}
	else if (ValueField == TEXT("BoolValue"))
	{
		Args->TryGetBoolField(TEXT("bool_value"), BV);
		WriteByName(ValueField, [&](FProperty* P, void* PP) {
			if (FBoolProperty* F = CastField<FBoolProperty>(P)) F->SetPropertyValue(PP, BV);
		});
	}
	else if (ValueField == TEXT("StringValue"))
	{
		Args->TryGetStringField(TEXT("string_value"), SV);
		WriteByName(ValueField, [&](FProperty* P, void* PP) {
			if (FStrProperty* F = CastField<FStrProperty>(P)) F->SetPropertyValue(PP, SV);
		});
	}
	else if (ValueField == TEXT("NameValue"))
	{
		Args->TryGetStringField(TEXT("name_value"), SV);
		WriteByName(ValueField, [&](FProperty* P, void* PP) {
			if (FNameProperty* F = CastField<FNameProperty>(P)) F->SetPropertyValue(PP, FName(*SV));
		});
	}
	else if (ValueField == TEXT("SoftObjectPathValue") || ValueField == TEXT("SoftClassPathValue"))
	{
		Args->TryGetStringField(TEXT("string_value"), SV);
		if (FStructProperty* PathProp = FindFProperty<FStructProperty>(TypesStruct, *ValueField))
		{
			void* PP = PathProp->ContainerPtrToValuePtr<void>(TypesPtr);
			PathProp->ImportText_Direct(*SV, PP, nullptr, PPF_None);
		}
	}
	else if (ValueField == TEXT("Vector2Value") || ValueField == TEXT("VectorValue") ||
	         ValueField == TEXT("Vector4Value") || ValueField == TEXT("QuatValue") ||
	         ValueField == TEXT("RotatorValue"))
	{
		double V[4]; FMemory::Memzero(V);
		const TCHAR* JsonKey = TEXT("vector_value");
		if (ValueField == TEXT("Vector2Value")) JsonKey = TEXT("vector2_value");
		else if (ValueField == TEXT("Vector4Value")) JsonKey = TEXT("vector4_value");
		else if (ValueField == TEXT("QuatValue")) JsonKey = TEXT("quat_value");
		else if (ValueField == TEXT("RotatorValue")) JsonKey = TEXT("rotator_value");
		const TSharedPtr<FJsonValue>* RawVal = nullptr;
		if (Args->Values.Find(JsonKey)) RawVal = &Args->Values[JsonKey];
		if (RawVal) ReadVectorArray(*RawVal, V);

		if (FStructProperty* SP = FindFProperty<FStructProperty>(TypesStruct, *ValueField))
		{
			void* PP = SP->ContainerPtrToValuePtr<void>(TypesPtr);
			if      (ValueField == TEXT("Vector2Value"))  *(FVector2D*)PP = FVector2D(V[0], V[1]);
			else if (ValueField == TEXT("VectorValue"))   *(FVector*)  PP = FVector  (V[0], V[1], V[2]);
			else if (ValueField == TEXT("Vector4Value"))  *(FVector4*) PP = FVector4 (V[0], V[1], V[2], V[3]);
			else if (ValueField == TEXT("QuatValue"))     *(FQuat*)    PP = FQuat    (V[0], V[1], V[2], V[3]);
			else if (ValueField == TEXT("RotatorValue"))  *(FRotator*) PP = FRotator (V[0], V[1], V[2]);
		}
	}

	FStructProperty* OutTargetProp = FindFProperty<FStructProperty>(Settings->GetClass(), TEXT("OutputTarget"));
	if (OutTargetProp)
	{
		void* TgtPtr = OutTargetProp->ContainerPtrToValuePtr<void>(Settings);
		FNameProperty* NameProp = FindFProperty<FNameProperty>(OutTargetProp->Struct, TEXT("AttributeName"));
		if (!NameProp) NameProp = FindFProperty<FNameProperty>(OutTargetProp->Struct, TEXT("Name"));
		if (NameProp)
		{
			NameProp->SetPropertyValue(NameProp->ContainerPtrToValuePtr<void>(TgtPtr), FName(*AttributeName));
		}
		else
		{
			const FString ImportStr = FString::Printf(TEXT("(AttributeName=\"%s\")"), *AttributeName);
			OutTargetProp->ImportText_Direct(*ImportStr, TgtPtr, Settings, PPF_None);
		}
	}

	Settings->MarkPackageDirty();
	Graph->MarkPackageDirty();

	TSharedPtr<FJsonObject> Out = MakeShareable(new FJsonObject);
	Out->SetBoolField(TEXT("success"), true);
	Out->SetStringField(TEXT("attribute_name"), AttributeName);
	Out->SetStringField(TEXT("type"), TypeStr);
	Out->SetStringField(TEXT("variant"), bIsPointVariant ? TEXT("point") : TEXT("set"));
	Out->SetStringField(TEXT("message"), FString::Printf(TEXT("CreateAttribute node[%d] configured: %s = (%s) constant"),
		(int32)NodeIndex, *AttributeName, *TypeStr));
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutJsonString);
	FJsonSerializer::Serialize(Out.ToSharedRef(), Writer);
}

namespace
{
	struct FPCGMathOpEntry { const TCHAR* Name; int64 EnumValue; int32 Arity; };
	static const FPCGMathOpEntry MathOps[] = {
		{ TEXT("Sign"),     1025, 1 }, { TEXT("Frac"),     1026, 1 },
		{ TEXT("Truncate"), 1027, 1 }, { TEXT("Round"),    1028, 1 },
		{ TEXT("Sqrt"),     1029, 1 }, { TEXT("Abs"),      1030, 1 },
		{ TEXT("Floor"),    1031, 1 }, { TEXT("Ceil"),     1032, 1 },
		{ TEXT("OneMinus"), 1033, 1 }, { TEXT("Inc"),      1034, 1 },
		{ TEXT("Dec"),      1035, 1 }, { TEXT("Negate"),   1036, 1 },
		{ TEXT("Add"),      2049, 2 }, { TEXT("Subtract"), 2050, 2 },
		{ TEXT("Multiply"), 2051, 2 }, { TEXT("Divide"),   2052, 2 },
		{ TEXT("Max"),      2053, 2 }, { TEXT("Min"),      2054, 2 },
		{ TEXT("Pow"),      2055, 2 }, { TEXT("ClampMin"), 2056, 2 },
		{ TEXT("ClampMax"), 2057, 2 }, { TEXT("Modulo"),   2058, 2 },
		{ TEXT("Set"),      2059, 2 },
		{ TEXT("Clamp"),    4097, 3 }, { TEXT("Lerp"),     4098, 3 },
	};

	void SetSelectorAttributeName(UObject* Container, const TCHAR* PropName, const FName& InName)
	{
		FStructProperty* SP = FindFProperty<FStructProperty>(Container->GetClass(), PropName);
		if (!SP) return;
		void* Ptr = SP->ContainerPtrToValuePtr<void>(Container);
		FNameProperty* NameProp = FindFProperty<FNameProperty>(SP->Struct, TEXT("AttributeName"));
		if (!NameProp) NameProp = FindFProperty<FNameProperty>(SP->Struct, TEXT("Name"));
		if (NameProp)
		{
			NameProp->SetPropertyValue(NameProp->ContainerPtrToValuePtr<void>(Ptr), InName);
		}
	}
}

void HandleSetPCGAttributeMathFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }

	FString GraphPath, OperationStr, InputA, InputB, InputC, OutputAttr;
	double NodeIndex = -1;
	bool bForceRoundingToInt = false, bForceOpToDouble = false;
	Args->TryGetStringField(TEXT("graph_path"),         GraphPath);
	Args->TryGetNumberField(TEXT("node_index"),         NodeIndex);
	Args->TryGetStringField(TEXT("operation"),          OperationStr);
	Args->TryGetStringField(TEXT("input_a"),            InputA);
	Args->TryGetStringField(TEXT("input_b"),            InputB);
	Args->TryGetStringField(TEXT("input_c"),            InputC);
	Args->TryGetStringField(TEXT("output_attribute"),   OutputAttr);
	Args->TryGetBoolField(TEXT("force_round_to_int"),   bForceRoundingToInt);
	Args->TryGetBoolField(TEXT("force_op_to_double"),   bForceOpToDouble);

	if (OperationStr.IsEmpty()) { OutError = TEXT("`operation` is required (e.g. \"Add\", \"Multiply\", \"Lerp\")."); return; }

	UPCGGraph* Graph = LoadPCGGraph(GraphPath, OutError);
	if (!Graph) return;
	TArray<UPCGNode*> All = GetVirtualNodeList(Graph);
	if (NodeIndex < 0 || NodeIndex >= All.Num()) { OutError = FString::Printf(TEXT("Node index %d out of range"), (int32)NodeIndex); return; }

	UPCGNode* Node = All[(int32)NodeIndex];
	UPCGMetadataMathsSettings* Maths = Cast<UPCGMetadataMathsSettings>(Node ? Node->GetSettings() : nullptr);
	if (!Maths)
	{
		OutError = FString::Printf(TEXT("Node[%d] is not a PCGMetadataMathsSettings node (class: %s)"),
			(int32)NodeIndex, Node && Node->GetSettings() ? *Node->GetSettings()->GetClass()->GetName() : TEXT("null"));
		return;
	}

	const FPCGMathOpEntry* MatchedOp = nullptr;
	for (const FPCGMathOpEntry& Op : MathOps)
	{
		if (OperationStr.Equals(Op.Name, ESearchCase::IgnoreCase)) { MatchedOp = &Op; break; }
	}
	if (!MatchedOp)
	{
		TArray<FString> Names;
		for (const FPCGMathOpEntry& Op : MathOps) Names.Add(Op.Name);
		OutError = FString::Printf(TEXT("Unknown operation '%s'. Valid: %s"), *OperationStr, *FString::Join(Names, TEXT(", ")));
		return;
	}

	if (FProperty* OpProp = FindFProperty<FProperty>(Maths->GetClass(), TEXT("Operation")))
	{
		void* OpPtr = OpProp->ContainerPtrToValuePtr<void>(Maths);
		if (FNumericProperty* NP = CastField<FNumericProperty>(OpProp))
		{
			NP->SetIntPropertyValue(OpPtr, MatchedOp->EnumValue);
		}
		else if (FEnumProperty* EP = CastField<FEnumProperty>(OpProp))
		{
			EP->GetUnderlyingProperty()->SetIntPropertyValue(OpPtr, MatchedOp->EnumValue);
		}
	}

	if (!InputA.IsEmpty()) SetSelectorAttributeName(Maths, TEXT("InputSource1"), FName(*InputA));
	if (MatchedOp->Arity >= 2 && !InputB.IsEmpty()) SetSelectorAttributeName(Maths, TEXT("InputSource2"), FName(*InputB));
	if (MatchedOp->Arity >= 3 && !InputC.IsEmpty()) SetSelectorAttributeName(Maths, TEXT("InputSource3"), FName(*InputC));

	if (!OutputAttr.IsEmpty()) SetSelectorAttributeName(Maths, TEXT("OutputTarget"), FName(*OutputAttr));

	Maths->bForceRoundingOpToInt = bForceRoundingToInt;
	Maths->bForceOpToDouble = bForceOpToDouble;

	Maths->MarkPackageDirty();
	Graph->MarkPackageDirty();

	TSharedPtr<FJsonObject> Out = MakeShareable(new FJsonObject);
	Out->SetBoolField(TEXT("success"), true);
	Out->SetStringField(TEXT("operation"), MatchedOp->Name);
	Out->SetNumberField(TEXT("arity"), MatchedOp->Arity);
	if (!InputA.IsEmpty())     Out->SetStringField(TEXT("input_a"), InputA);
	if (!InputB.IsEmpty())     Out->SetStringField(TEXT("input_b"), InputB);
	if (!InputC.IsEmpty())     Out->SetStringField(TEXT("input_c"), InputC);
	if (!OutputAttr.IsEmpty()) Out->SetStringField(TEXT("output_attribute"), OutputAttr);
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutJsonString);
	FJsonSerializer::Serialize(Out.ToSharedRef(), Writer);
}

// ===============================================================================================
// get_pcg_level_summary — read-only overview of every PCG actor in the level: bounds, generation
// state, instance counts, and pairwise overlaps. Written so the AI can check "am I about to stack
// a 4th full-map forest layer?" BEFORE calling spawn_pcg_actor/generate_pcg again.
// ===============================================================================================
void HandleGetPCGLevelSummary(FString& OutJsonString, FString& OutError)
{
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!World) { OutError = TEXT("No editor world available"); return; }

	TArray<FPCGActorSummary> Actors = GatherPCGActorSummaries(World);

	int32 TotalInstances = 0;
	int32 GenerateOnLoadCount = 0;
	TArray<TSharedPtr<FJsonValue>> ActorsJson;
	TArray<FString> Warnings;
	TArray<FString> ReportedPairs; // dedupe warning text per unordered pair

	for (int32 i = 0; i < Actors.Num(); i++)
	{
		const FPCGActorSummary& S = Actors[i];
		TotalInstances += S.TotalInstances;
		if (S.GenerationTrigger == TEXT("GenerateOnLoad")) GenerateOnLoadCount++;

		TSharedPtr<FJsonObject> AObj = MakeShareable(new FJsonObject);
		AObj->SetStringField(TEXT("actor_label"), S.Label);
		AObj->SetStringField(TEXT("graph_path"), S.GraphPath);

		TSharedPtr<FJsonObject> BoundsObj = MakeShareable(new FJsonObject);
		BoundsObj->SetNumberField(TEXT("center_x"), S.Center.X);
		BoundsObj->SetNumberField(TEXT("center_y"), S.Center.Y);
		BoundsObj->SetNumberField(TEXT("center_z"), S.Center.Z);
		BoundsObj->SetNumberField(TEXT("extent_x"), S.Extent.X);
		BoundsObj->SetNumberField(TEXT("extent_y"), S.Extent.Y);
		BoundsObj->SetNumberField(TEXT("extent_z"), S.Extent.Z);
		AObj->SetObjectField(TEXT("bounds"), BoundsObj);

		AObj->SetStringField(TEXT("generation_trigger"), S.GenerationTrigger);
		AObj->SetBoolField(TEXT("generated"), S.bGenerated);
		AObj->SetBoolField(TEXT("activated"), S.bActivated);
		AObj->SetNumberField(TEXT("ism_components"), S.ISMComponents);
		AObj->SetNumberField(TEXT("total_instances"), S.TotalInstances);

		TArray<TSharedPtr<FJsonValue>> Overlaps;
		for (int32 j = 0; j < Actors.Num(); j++)
		{
			if (i == j) continue;
			const FPCGActorSummary& Other = Actors[j];
			const float Fraction = ComputePCGBoxOverlapFractionXY(S.Center, S.Extent, Other.Center, Other.Extent);
			if (Fraction < 0.25f) continue;

			TSharedPtr<FJsonObject> O = MakeShareable(new FJsonObject);
			O->SetStringField(TEXT("actor_label"), Other.Label);
			O->SetNumberField(TEXT("overlap_fraction"), Fraction);
			Overlaps.Add(MakeShareable(new FJsonValueObject(O)));

			const FString PairKey = i < j ? (S.Label + TEXT("|") + Other.Label) : (Other.Label + TEXT("|") + S.Label);
			if (!ReportedPairs.Contains(PairKey))
			{
				ReportedPairs.Add(PairKey);
				if (Fraction >= 0.9f)
				{
					Warnings.Add(FString::Printf(
						TEXT("%s overlaps %s by %.0f%% — stacked layers multiply density; split into zones or delete one."),
						*S.Label, *Other.Label, Fraction * 100.f));
				}
				else
				{
					Warnings.Add(FString::Printf(TEXT("%s overlaps %s by %.0f%%."), *S.Label, *Other.Label, Fraction * 100.f));
				}
			}
		}
		if (Overlaps.Num() > 0) AObj->SetArrayField(TEXT("overlaps"), Overlaps);

		ActorsJson.Add(MakeShareable(new FJsonValueObject(AObj)));
	}

	const int32 Budget = GetPCGInstanceBudget();
	const bool bOverBudget = TotalInstances > Budget;

	if (GenerateOnLoadCount > 0)
	{
		Warnings.Add(FString::Printf(
			TEXT("%d actor(s) use GenerateOnLoad — regenerates on every map load; prefer GenerateOnDemand for baked worlds."),
			GenerateOnLoadCount));
	}
	if (bOverBudget)
	{
		Warnings.Add(FString::Printf(TEXT("Total PCG instances (%d) exceed the configured budget (%d)."), TotalInstances, Budget));
	}

	TSharedPtr<FJsonObject> Out = MakeShareable(new FJsonObject);
	Out->SetBoolField(TEXT("success"), true);
	Out->SetArrayField(TEXT("actors"), ActorsJson);
	Out->SetNumberField(TEXT("actor_count"), Actors.Num());
	Out->SetNumberField(TEXT("total_instances"), TotalInstances);
	Out->SetNumberField(TEXT("budget"), Budget);
	Out->SetBoolField(TEXT("over_budget"), bOverBudget);

	TArray<TSharedPtr<FJsonValue>> WarningsJson;
	for (const FString& W : Warnings) WarningsJson.Add(MakeShareable(new FJsonValueString(W)));
	Out->SetArrayField(TEXT("warnings"), WarningsJson);

	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutJsonString);
	FJsonSerializer::Serialize(Out.ToSharedRef(), Writer);
}

// ===============================================================================================
// set_pcg_exclusion — wires a Difference node (Source=old upstream, Differences=exclusion sources,
// Out=spawner.In) in front of one or more StaticMeshSpawner nodes so vegetation/props don't land
// inside existing houses, other PCG actors' output, or along roads. Idempotent: exclusion-source
// nodes and the per-spawner Difference node are tagged via NodeComment markers and reused on
// repeat calls instead of being duplicated.
// ===============================================================================================
void HandleSetPCGExclusion(const FString& GraphPath, bool bWorldCollision,
	const TArray<FString>& ActorTags, const TArray<FString>& ActorClasses,
	const TArray<FString>& SplineTags, float SplineWidth, float Margin,
	const TArray<int32>& TargetNodesIn,
	FString& OutJsonString, FString& OutError)
{
	UPCGGraph* Graph = LoadPCGGraph(GraphPath, OutError);
	if (!Graph) return;

	// 1. Resolve target spawner nodes (default: every StaticMeshSpawner in the graph).
	TArray<UPCGNode*> TargetSpawners;
	if (TargetNodesIn.Num() > 0)
	{
		for (int32 Idx : TargetNodesIn)
		{
			UPCGNode* Node = GetNodeByVirtualIndex(Graph, Idx);
			if (!Node) { OutError = FString::Printf(TEXT("target_nodes: index %d out of range"), Idx); return; }
			if (!Cast<UPCGStaticMeshSpawnerSettings>(Node->GetSettings()))
			{
				OutError = FString::Printf(TEXT("target_nodes: node[%d] is not a StaticMeshSpawner"), Idx);
				return;
			}
			TargetSpawners.AddUnique(Node);
		}
	}
	else
	{
		for (UPCGNode* Node : Graph->GetNodes())
		{
			if (Node && Cast<UPCGStaticMeshSpawnerSettings>(Node->GetSettings()))
				TargetSpawners.Add(Node);
		}
	}

	if (TargetSpawners.Num() == 0)
	{
		OutError = TEXT("No StaticMeshSpawner nodes found in this graph. Specify target_nodes explicitly, or add a spawner first.");
		return;
	}

	const FScopedTransaction Transaction(NSLOCTEXT("AxivorAI", "SetPCGExclusion", "Set PCG Exclusion"));
	Graph->Modify();

	TArray<FString> Errors;

	// 2. Resolve/create the requested exclusion source nodes. These are global to the graph and
	// reused across every target spawner (and across repeat calls) via NodeComment markers.
	struct FExclusionSource
	{
		UPCGNode* Node = nullptr;
		FString Description;
		bool bReused = false;
	};
	TArray<FExclusionSource> Sources;

	const int32 SourcePosX = -900;
	int32 NextSourcePosY = -400;
	auto AllocatePosY = [&]() -> int32 { const int32 Y = NextSourcePosY; NextSourcePosY += 220; return Y; };

	if (bWorldCollision)
	{
		const FString Marker = PCGExclusionMarkers::World();
		if (UPCGNode* Existing = PCGExclusionMarkers::FindByMarker(Graph, Marker))
		{
			Sources.Add({ Existing, TEXT("world collision"), true });
		}
		else
		{
			UPCGSettings* DefaultSettings = nullptr;
			UPCGNode* Node = Graph->AddNodeOfType(UPCGWorldQuerySettings::StaticClass(), DefaultSettings);
			if (!Node) { Errors.Add(TEXT("Failed to create WorldQuery exclusion node")); }
			else
			{
				if (UPCGWorldQuerySettings* WQ = Cast<UPCGWorldQuerySettings>(Node->GetSettings()))
				{
					WQ->QueryParams.bIgnorePCGHits = false;
					WQ->QueryParams.bIgnoreSelfHits = true;
					WQ->QueryParams.CollisionChannel = ECC_WorldStatic;
					WQ->QueryParams.SelectLandscapeHits = EPCGWorldQuerySelectLandscapeHits::Exclude;
					WQ->QueryParams.bSearchForOverlap = true;
					WQ->MarkPackageDirty();
				}
#if WITH_EDITOR
				Node->SetNodePosition(SourcePosX, AllocatePosY());
#endif
				Node->NodeComment = Marker + TEXT(" World collision (houses/props). Managed by set_pcg_exclusion.");
				Node->bCommentBubbleVisible = true;
				Sources.Add({ Node, TEXT("world collision"), false });
			}
		}
	}

	const float ScaleFactor = 1.f + Margin;

	for (const FString& Tag : ActorTags)
	{
		if (Tag.IsEmpty()) continue;
		const FString Marker = PCGExclusionMarkers::Tag(Tag);
		if (UPCGNode* Existing = PCGExclusionMarkers::FindByMarker(Graph, Marker))
		{
			Sources.Add({ Existing, FString::Printf(TEXT("actor tag '%s'"), *Tag), true });
			continue;
		}

		UPCGSettings* DFADefault = nullptr;
		UPCGNode* DFANode = Graph->AddNodeOfType(UPCGDataFromActorSettings::StaticClass(), DFADefault);
		if (!DFANode) { Errors.Add(FString::Printf(TEXT("Failed to create DataFromActor node for tag '%s'"), *Tag)); continue; }
		if (UPCGDataFromActorSettings* DFA = Cast<UPCGDataFromActorSettings>(DFANode->GetSettings()))
		{
			DFA->ActorSelector.ActorFilter = EPCGActorFilter::AllWorldActors;
			DFA->ActorSelector.ActorSelection = EPCGActorSelection::ByTag;
			DFA->ActorSelector.ActorSelectionTag = FName(*Tag);
			DFA->Mode = EPCGGetDataFromActorMode::ParseActorComponents;
			DFA->MarkPackageDirty();
		}
#if WITH_EDITOR
		const int32 RowY = AllocatePosY();
		DFANode->SetNodePosition(SourcePosX, RowY);
#endif

		UPCGSettings* BMDefault = nullptr;
		UPCGNode* BMNode = Graph->AddNodeOfType(UPCGBoundsModifierSettings::StaticClass(), BMDefault);
		if (!BMNode) { Errors.Add(FString::Printf(TEXT("Failed to create BoundsModifier node for tag '%s'"), *Tag)); continue; }
		if (UPCGBoundsModifierSettings* BM = Cast<UPCGBoundsModifierSettings>(BMNode->GetSettings()))
		{
			BM->Mode = EPCGBoundsModifierMode::Scale;
			BM->BoundsMin = FVector(ScaleFactor, ScaleFactor, ScaleFactor);
			BM->BoundsMax = FVector(ScaleFactor, ScaleFactor, ScaleFactor);
			BM->MarkPackageDirty();
		}
#if WITH_EDITOR
		BMNode->SetNodePosition(SourcePosX + 260, RowY);
#endif

		if (UPCGPin* DFAOut = DFANode->GetOutputPin(PCGExclusionMarkers::OutPin))
			if (UPCGPin* BMIn = BMNode->GetInputPin(PCGExclusionMarkers::InPin))
				DFAOut->AddEdgeTo(BMIn);

		BMNode->NodeComment = Marker + FString::Printf(TEXT(" Excludes actors tagged '%s' (scaled x%.2f). Managed by set_pcg_exclusion."), *Tag, ScaleFactor);
		BMNode->bCommentBubbleVisible = true;
		Sources.Add({ BMNode, FString::Printf(TEXT("actor tag '%s'"), *Tag), false });
	}

	for (const FString& ClassName : ActorClasses)
	{
		if (ClassName.IsEmpty()) continue;

		UClass* Cls = FindObject<UClass>(nullptr, *ClassName);
		if (!Cls) Cls = LoadObject<UClass>(nullptr, *ClassName);
		if (!Cls) Cls = FindObject<UClass>(nullptr, *(TEXT("/Script/Engine.") + ClassName));
		if (!Cls) Cls = LoadObject<UClass>(nullptr, *(TEXT("/Script/Engine.") + ClassName));
		if (!Cls) { Errors.Add(FString::Printf(TEXT("actor_classes: could not resolve class '%s'"), *ClassName)); continue; }

		const FString Marker = PCGExclusionMarkers::Class(ClassName);
		if (UPCGNode* Existing = PCGExclusionMarkers::FindByMarker(Graph, Marker))
		{
			Sources.Add({ Existing, FString::Printf(TEXT("actor class '%s'"), *ClassName), true });
			continue;
		}

		UPCGSettings* DFADefault = nullptr;
		UPCGNode* DFANode = Graph->AddNodeOfType(UPCGDataFromActorSettings::StaticClass(), DFADefault);
		if (!DFANode) { Errors.Add(FString::Printf(TEXT("Failed to create DataFromActor node for class '%s'"), *ClassName)); continue; }
		if (UPCGDataFromActorSettings* DFA = Cast<UPCGDataFromActorSettings>(DFANode->GetSettings()))
		{
			DFA->ActorSelector.ActorFilter = EPCGActorFilter::AllWorldActors;
			DFA->ActorSelector.ActorSelection = EPCGActorSelection::ByClass;
			DFA->ActorSelector.ActorSelectionClass = Cls;
			DFA->Mode = EPCGGetDataFromActorMode::ParseActorComponents;
			DFA->MarkPackageDirty();
		}
#if WITH_EDITOR
		const int32 RowY = AllocatePosY();
		DFANode->SetNodePosition(SourcePosX, RowY);
#endif

		UPCGSettings* BMDefault = nullptr;
		UPCGNode* BMNode = Graph->AddNodeOfType(UPCGBoundsModifierSettings::StaticClass(), BMDefault);
		if (!BMNode) { Errors.Add(FString::Printf(TEXT("Failed to create BoundsModifier node for class '%s'"), *ClassName)); continue; }
		if (UPCGBoundsModifierSettings* BM = Cast<UPCGBoundsModifierSettings>(BMNode->GetSettings()))
		{
			BM->Mode = EPCGBoundsModifierMode::Scale;
			BM->BoundsMin = FVector(ScaleFactor, ScaleFactor, ScaleFactor);
			BM->BoundsMax = FVector(ScaleFactor, ScaleFactor, ScaleFactor);
			BM->MarkPackageDirty();
		}
#if WITH_EDITOR
		BMNode->SetNodePosition(SourcePosX + 260, RowY);
#endif

		if (UPCGPin* DFAOut = DFANode->GetOutputPin(PCGExclusionMarkers::OutPin))
			if (UPCGPin* BMIn = BMNode->GetInputPin(PCGExclusionMarkers::InPin))
				DFAOut->AddEdgeTo(BMIn);

		BMNode->NodeComment = Marker + FString::Printf(TEXT(" Excludes actors of class '%s' (scaled x%.2f). Managed by set_pcg_exclusion."), *ClassName, ScaleFactor);
		BMNode->bCommentBubbleVisible = true;
		Sources.Add({ BMNode, FString::Printf(TEXT("actor class '%s'"), *ClassName), false });
	}

	for (const FString& Tag : SplineTags)
	{
		if (Tag.IsEmpty()) continue;
		const FString Marker = PCGExclusionMarkers::Spline(Tag);
		if (UPCGNode* Existing = PCGExclusionMarkers::FindByMarker(Graph, Marker))
		{
			Sources.Add({ Existing, FString::Printf(TEXT("spline tag '%s' (%.0fcm wide)"), *Tag, SplineWidth), true });
			continue;
		}

		UPCGSettings* DFADefault = nullptr;
		UPCGNode* DFANode = Graph->AddNodeOfType(UPCGDataFromActorSettings::StaticClass(), DFADefault);
		if (!DFANode) { Errors.Add(FString::Printf(TEXT("Failed to create DataFromActor node for spline tag '%s'"), *Tag)); continue; }
		if (UPCGDataFromActorSettings* DFA = Cast<UPCGDataFromActorSettings>(DFANode->GetSettings()))
		{
			DFA->ActorSelector.ActorFilter = EPCGActorFilter::AllWorldActors;
			DFA->ActorSelector.ActorSelection = EPCGActorSelection::ByTag;
			DFA->ActorSelector.ActorSelectionTag = FName(*Tag);
			DFA->Mode = EPCGGetDataFromActorMode::ParseActorComponents;
			DFA->MarkPackageDirty();
		}
#if WITH_EDITOR
		const int32 RowY = AllocatePosY();
		DFANode->SetNodePosition(SourcePosX, RowY);
#endif

		UPCGSettings* SSDefault = nullptr;
		UPCGNode* SSNode = Graph->AddNodeOfType(UPCGSplineSamplerSettings::StaticClass(), SSDefault);
		if (!SSNode) { Errors.Add(FString::Printf(TEXT("Failed to create SplineSampler node for spline tag '%s'"), *Tag)); continue; }
		if (UPCGSplineSamplerSettings* SS = Cast<UPCGSplineSamplerSettings>(SSNode->GetSettings()))
		{
			SS->SamplerParams.Dimension = EPCGSplineSamplingDimension::OnSpline;
			SS->SamplerParams.Mode = EPCGSplineSamplingMode::Distance;
			SS->SamplerParams.DistanceIncrement = FMath::Max(1.f, SplineWidth * 0.5f);
			SS->MarkPackageDirty();
		}
#if WITH_EDITOR
		SSNode->SetNodePosition(SourcePosX + 260, RowY);
#endif

		UPCGSettings* BMDefault = nullptr;
		UPCGNode* BMNode = Graph->AddNodeOfType(UPCGBoundsModifierSettings::StaticClass(), BMDefault);
		if (!BMNode) { Errors.Add(FString::Printf(TEXT("Failed to create BoundsModifier node for spline tag '%s'"), *Tag)); continue; }
		if (UPCGBoundsModifierSettings* BM = Cast<UPCGBoundsModifierSettings>(BMNode->GetSettings()))
		{
			const float HalfW = SplineWidth * 0.5f;
			BM->Mode = EPCGBoundsModifierMode::Set;
			BM->BoundsMin = FVector(-HalfW, -HalfW, -HalfW);
			BM->BoundsMax = FVector(HalfW, HalfW, HalfW);
			BM->MarkPackageDirty();
		}
#if WITH_EDITOR
		BMNode->SetNodePosition(SourcePosX + 520, RowY);
#endif

		if (UPCGPin* DFAOut = DFANode->GetOutputPin(PCGExclusionMarkers::OutPin))
			if (UPCGPin* SSSplineIn = SSNode->GetInputPin(PCGSplineSamplerConstants::SplineLabel))
				DFAOut->AddEdgeTo(SSSplineIn);

		if (UPCGPin* SSOut = SSNode->GetOutputPin(PCGExclusionMarkers::OutPin))
			if (UPCGPin* BMIn = BMNode->GetInputPin(PCGExclusionMarkers::InPin))
				SSOut->AddEdgeTo(BMIn);

		BMNode->NodeComment = Marker + FString::Printf(TEXT(" Excludes a %.0fcm-wide corridor along splines tagged '%s'. Managed by set_pcg_exclusion."), SplineWidth, *Tag);
		BMNode->bCommentBubbleVisible = true;
		Sources.Add({ BMNode, FString::Printf(TEXT("spline tag '%s' (%.0fcm wide)"), *Tag, SplineWidth), false });
	}

	if (Sources.Num() == 0)
	{
		OutError = Errors.Num() > 0
			? FString::Join(Errors, TEXT("; "))
			: TEXT("No exclusion sources requested. Set world_collision=true, or provide actor_tags/actor_classes/spline_tags.");
		return;
	}

	// 3. Wire each target spawner behind a Difference node fed by every requested exclusion source.
	TArray<TSharedPtr<FJsonValue>> DifferenceNodesJson;
	int32 SpawnersWired = 0;

	for (UPCGNode* Spawner : TargetSpawners)
	{
		UPCGPin* SpawnerIn = Spawner->GetInputPin(PCGExclusionMarkers::InPin);
		if (!SpawnerIn) { Errors.Add(FString::Printf(TEXT("Spawner node[%d] has no 'In' pin"), GetVirtualIndexOfNode(Graph, Spawner))); continue; }

		UPCGNode* DiffNode = nullptr;
		bool bReusedDiff = false;

		// Already sitting directly upstream of this spawner?
		if (SpawnerIn->Edges.Num() > 0)
		{
			UPCGEdge* FirstEdge = SpawnerIn->Edges[0].Get();
			UPCGPin* UpstreamPin = FirstEdge ? FirstEdge->InputPin.Get() : nullptr;
			UPCGNode* UpstreamNode = UpstreamPin ? UpstreamPin->Node.Get() : nullptr;
			if (PCGExclusionMarkers::IsExclusionDifference(UpstreamNode))
			{
				DiffNode = UpstreamNode;
				bReusedDiff = true;
			}
		}

		if (!DiffNode)
		{
			// Detach every edge currently feeding Spawner.In — we'll re-route it through Difference.Source.
			TArray<TPair<UPCGNode*, FName>> UpstreamConnections;
			TArray<TObjectPtr<UPCGEdge>> EdgesCopy = SpawnerIn->Edges;
			for (const TObjectPtr<UPCGEdge>& EdgePtr : EdgesCopy)
			{
				UPCGEdge* Edge = EdgePtr.Get();
				if (!Edge || !Edge->InputPin) continue;
				UPCGPin* UpstreamPin = Edge->InputPin.Get();
				UPCGNode* UpstreamNode = UpstreamPin ? UpstreamPin->Node.Get() : nullptr;
				if (!UpstreamNode) continue;
				UpstreamConnections.Add(TPair<UPCGNode*, FName>(UpstreamNode, UpstreamPin->Properties.Label));
				Graph->RemoveEdge(UpstreamNode, UpstreamPin->Properties.Label, Spawner, PCGExclusionMarkers::InPin);
			}

			UPCGSettings* DiffDefault = nullptr;
			DiffNode = Graph->AddNodeOfType(UPCGDifferenceSettings::StaticClass(), DiffDefault);
			if (!DiffNode) { Errors.Add(TEXT("Failed to create Difference node")); continue; }
			if (UPCGDifferenceSettings* Diff = Cast<UPCGDifferenceSettings>(DiffNode->GetSettings()))
			{
				Diff->Mode = EPCGDifferenceMode::Inferred;
				Diff->DensityFunction = EPCGDifferenceDensityFunction::Minimum;
				Diff->MarkPackageDirty();
			}

#if WITH_EDITOR
			int32 SpawnerPosX = 0, SpawnerPosY = 0;
			Spawner->GetNodePosition(SpawnerPosX, SpawnerPosY);
			DiffNode->SetNodePosition(SpawnerPosX - 300, SpawnerPosY);
#endif
			DiffNode->NodeComment = FString(PCGExclusionMarkers::Diff()) + TEXT(" Keeps vegetation out of exclusion zones (buildings, roads). Managed by set_pcg_exclusion.");
			DiffNode->bCommentBubbleVisible = true;

			// Re-route the original upstream connection(s) into Difference.Source.
			UPCGPin* DiffSourceIn = DiffNode->GetInputPin(PCGDifferenceConstants::SourceLabel);
			for (const TPair<UPCGNode*, FName>& Conn : UpstreamConnections)
			{
				if (UPCGPin* UpOut = Conn.Key->GetOutputPin(Conn.Value))
					if (DiffSourceIn) UpOut->AddEdgeTo(DiffSourceIn);
			}

			// Difference.Out -> Spawner.In
			if (UPCGPin* DiffOut = DiffNode->GetOutputPin(PCGExclusionMarkers::OutPin))
				DiffOut->AddEdgeTo(SpawnerIn);
		}

		// Connect every requested exclusion source -> Difference.Differences (no-op if already wired).
		UPCGPin* DiffDifferencesIn = DiffNode->GetInputPin(PCGDifferenceConstants::DifferencesLabel);
		for (const FExclusionSource& Src : Sources)
		{
			if (!Src.Node) continue;
			if (UPCGPin* SrcOut = Src.Node->GetOutputPin(PCGExclusionMarkers::OutPin))
				if (DiffDifferencesIn) SrcOut->AddEdgeTo(DiffDifferencesIn);
		}

		TSharedPtr<FJsonObject> DObj = MakeShareable(new FJsonObject);
		DObj->SetNumberField(TEXT("spawner_node_index"), GetVirtualIndexOfNode(Graph, Spawner));
		DObj->SetNumberField(TEXT("difference_node_index"), GetVirtualIndexOfNode(Graph, DiffNode));
		DObj->SetBoolField(TEXT("reused"), bReusedDiff);
		DifferenceNodesJson.Add(MakeShareable(new FJsonValueObject(DObj)));
		SpawnersWired++;
	}

	TArray<TSharedPtr<FJsonValue>> ExclusionNodesJson;
	for (const FExclusionSource& Src : Sources)
	{
		if (!Src.Node) continue;
		TSharedPtr<FJsonObject> EObj = MakeShareable(new FJsonObject);
		EObj->SetNumberField(TEXT("node_index"), GetVirtualIndexOfNode(Graph, Src.Node));
		EObj->SetStringField(TEXT("class"), Src.Node->GetSettings() ? Src.Node->GetSettings()->GetClass()->GetName() : TEXT("None"));
		EObj->SetStringField(TEXT("excludes"), Src.Description);
		EObj->SetBoolField(TEXT("reused"), Src.bReused);
		ExclusionNodesJson.Add(MakeShareable(new FJsonValueObject(EObj)));
	}

	Graph->MarkPackageDirty();
	if (UWorld* PCGWorld = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr)
	{
		if (UPCGSubsystem* Sub = PCGWorld->GetSubsystem<UPCGSubsystem>())
			Sub->NotifyGraphChanged(Graph, EPCGChangeType::Structural);
	}

	TSharedPtr<FJsonObject> Out = MakeShareable(new FJsonObject);
	Out->SetBoolField(TEXT("success"), SpawnersWired > 0);
	Out->SetArrayField(TEXT("difference_nodes"), DifferenceNodesJson);
	Out->SetArrayField(TEXT("exclusion_nodes"), ExclusionNodesJson);
	Out->SetNumberField(TEXT("spawners_wired"), SpawnersWired);
	if (Errors.Num() > 0) Out->SetStringField(TEXT("errors"), FString::Join(Errors, TEXT("; ")));
	Out->SetStringField(TEXT("hint"), TEXT("regenerate with generate_pcg(force=true)"));
	Out->SetStringField(TEXT("message"), FString::Printf(
		TEXT("Wired %d exclusion source(s) into %d spawner(s) via Difference node(s)."), Sources.Num(), SpawnersWired));

	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutJsonString);
	FJsonSerializer::Serialize(Out.ToSharedRef(), Writer);
}

void HandleGetPCGLevelSummaryFromArgs(const TSharedPtr<FJsonObject>& /*Args*/, FString& OutJsonString, FString& OutError)
{
	HandleGetPCGLevelSummary(OutJsonString, OutError);
}

void HandleSetPCGExclusionFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }

	FString GraphPath;
	Args->TryGetStringField(TEXT("graph_path"), GraphPath);

	bool bWorldCollision = true;
	Args->TryGetBoolField(TEXT("world_collision"), bWorldCollision);

	auto ReadStringArray = [&Args](const TCHAR* Key) -> TArray<FString>
	{
		TArray<FString> Result;
		const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
		if (Args->TryGetArrayField(Key, Arr) && Arr)
		{
			for (const TSharedPtr<FJsonValue>& V : *Arr)
			{
				FString S;
				if (V.IsValid() && V->TryGetString(S) && !S.IsEmpty()) Result.Add(S);
			}
		}
		return Result;
	};

	TArray<FString> ActorTags = ReadStringArray(TEXT("actor_tags"));
	TArray<FString> ActorClasses = ReadStringArray(TEXT("actor_classes"));
	TArray<FString> SplineTags = ReadStringArray(TEXT("spline_tags"));

	double SplineWidth = 600.0, Margin = 0.15;
	Args->TryGetNumberField(TEXT("spline_width"), SplineWidth);
	Args->TryGetNumberField(TEXT("margin"), Margin);

	TArray<int32> TargetNodes;
	const TArray<TSharedPtr<FJsonValue>>* TargetArr = nullptr;
	if (Args->TryGetArrayField(TEXT("target_nodes"), TargetArr) && TargetArr)
	{
		for (const TSharedPtr<FJsonValue>& V : *TargetArr)
		{
			if (V.IsValid()) TargetNodes.Add((int32)V->AsNumber());
		}
	}

	HandleSetPCGExclusion(GraphPath, bWorldCollision, ActorTags, ActorClasses, SplineTags,
		(float)SplineWidth, (float)Margin, TargetNodes, OutJsonString, OutError);
}

}
