// Copyright 2026, BlueprintsLab, All rights reserved.

#include "Tools/AudioTools.h"
#include "Tools/BatchToolHelper.h"
#include "Tools/AssetCreationHelper.h"
#include "Managers/SettingsManager.h"
#include "Managers/EditorProfileSync.h"
#include "UECPCoreModule.h"
#include "Services/IUECPAssetGenService.h"
#include "ApiKeyManager.h"
#include "MCPToolsLog.h"
#include "EditorAssetLibrary.h"
#include "AssetToolsModule.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "UObject/SavePackage.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"

#include "Sound/SoundCue.h"
#include "Sound/SoundWave.h"
#include "Sound/SoundAttenuation.h"
#include "Sound/SoundSubmix.h"
#include "Sound/SoundEffectSubmix.h"
#include "Sound/SoundNode.h"
#include "Sound/SoundNodeWavePlayer.h"
#include "SoundCueGraph/SoundCueGraphNode.h"
#include "Sound/SoundNodeRandom.h"
#include "Sound/SoundNodeMixer.h"
#include "Sound/SoundNodeModulator.h"
#include "Sound/SoundNodeLooping.h"
#include "Sound/SoundNodeDelay.h"
#include "Sound/SoundNodeAttenuation.h"
#include "Sound/SoundNodeEnveloper.h"
#include "Sound/SoundNodeConcatenator.h"
#include "Sound/SoundNodeSwitch.h"
#include "Factories/SoundCueFactoryNew.h"
#include "Sound/SoundAttenuation.h"
#include "Sound/SoundClass.h"
#include "Sound/SoundMix.h"
#include "Sound/SoundSubmix.h"
#include "Sound/SoundConcurrency.h"
#include "Sound/SoundBase.h"
#include "Factories/SoundClassFactory.h"
#include "UObject/Package.h"
#include "Misc/PackageName.h"
#include "UObject/UnrealType.h"
#include "Containers/Set.h"
#include "SoundControlBus.h"
#include "SoundControlBusMix.h"
#include "Quartz/QuartzSubsystem.h"
#include "Quartz/AudioMixerClockHandle.h"
#include "Editor.h"

namespace AudioTools
{

static void SetError(const FString& Msg, FString& OutJsonString, FString& OutError)
{
	OutError = Msg;
	TSharedPtr<FJsonObject> Obj = MakeShareable(new FJsonObject);
	Obj->SetBoolField(TEXT("success"), false);
	Obj->SetStringField(TEXT("error"), Msg);
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutJsonString);
	FJsonSerializer::Serialize(Obj.ToSharedRef(), Writer);
}

static void BuildSuccessJson(const TSharedPtr<FJsonObject>& Obj, FString& OutJsonString)
{
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutJsonString);
	FJsonSerializer::Serialize(Obj.ToSharedRef(), Writer);
}

static void CollectAllNodes(const USoundCue* SoundCue, TArray<USoundNode*>& OutNodes)
{
	if (!SoundCue) return;
	TArray<USoundNode*> Queue;
	if (SoundCue->FirstNode) Queue.Add(SoundCue->FirstNode);
	while (Queue.Num() > 0)
	{
		USoundNode* Node = Queue[0];
		Queue.RemoveAt(0);
		if (!Node || OutNodes.Contains(Node)) continue;
		OutNodes.Add(Node);
		for (USoundNode* Child : Node->ChildNodes)
		{
			if (Child) Queue.Add(Child);
		}
	}
}

static void SetupGraphNode(USoundCue* SoundCue, USoundNode* SoundNode, int32 PosX = 0, int32 PosY = 0)
{
	if (!SoundCue || !SoundNode) return;
	SoundCue->AllNodes.AddUnique(SoundNode);
	SoundCue->SetupSoundNode(SoundNode, false);
	if (USoundCueGraphNode* GraphNode = Cast<USoundCueGraphNode>(SoundNode->GraphNode))
	{
		GraphNode->NodePosX = PosX;
		GraphNode->NodePosY = PosY;
		GraphNode->ReconstructNode();
	}
}

static USoundNode* FindNodeByName(USoundCue* SoundCue, const FString& NodeName)
{
	TArray<USoundNode*> AllNodes;
	CollectAllNodes(SoundCue, AllNodes);
	for (USoundNode* Node : AllNodes)
	{
		if (Node && Node->GetName().Equals(NodeName, ESearchCase::IgnoreCase))
			return Node;
	}
	UPackage* Pkg = SoundCue->GetOutermost();
	for (TObjectIterator<USoundNode> It; It; ++It)
	{
		if (It->IsIn(Pkg) && It->GetName().Equals(NodeName, ESearchCase::IgnoreCase))
			return *It;
	}
	return nullptr;
}

static USoundNode* CreateNodeOfType(USoundCue* SoundCue, const FString& NodeType, const FString& SoundWavePath)
{
	USoundNode* NewNode = nullptr;

	if (NodeType.Equals(TEXT("Wave"), ESearchCase::IgnoreCase))
	{
		USoundNodeWavePlayer* WaveNode = NewObject<USoundNodeWavePlayer>(SoundCue, NAME_None, RF_Transactional);
		if (!SoundWavePath.IsEmpty())
		{
			USoundWave* SoundWave = Cast<USoundWave>(UEditorAssetLibrary::LoadAsset(SoundWavePath));
			if (SoundWave) WaveNode->SetSoundWave(SoundWave);
		}
		NewNode = WaveNode;
	}
	else if (NodeType.Equals(TEXT("Random"), ESearchCase::IgnoreCase))
	{
		NewNode = NewObject<USoundNodeRandom>(SoundCue, NAME_None, RF_Transactional);
	}
	else if (NodeType.Equals(TEXT("Mixer"), ESearchCase::IgnoreCase))
	{
		NewNode = NewObject<USoundNodeMixer>(SoundCue, NAME_None, RF_Transactional);
	}
	else if (NodeType.Equals(TEXT("Modulator"), ESearchCase::IgnoreCase))
	{
		NewNode = NewObject<USoundNodeModulator>(SoundCue, NAME_None, RF_Transactional);
	}
	else if (NodeType.Equals(TEXT("Looping"), ESearchCase::IgnoreCase))
	{
		USoundNodeLooping* LoopNode = NewObject<USoundNodeLooping>(SoundCue, NAME_None, RF_Transactional);
		LoopNode->bLoopIndefinitely = true;
		NewNode = LoopNode;
	}
	else if (NodeType.Equals(TEXT("Delay"), ESearchCase::IgnoreCase))
	{
		NewNode = NewObject<USoundNodeDelay>(SoundCue, NAME_None, RF_Transactional);
	}
	else if (NodeType.Equals(TEXT("Attenuation"), ESearchCase::IgnoreCase))
	{
		NewNode = NewObject<USoundNodeAttenuation>(SoundCue, NAME_None, RF_Transactional);
	}
	else if (NodeType.Equals(TEXT("Enveloper"), ESearchCase::IgnoreCase))
	{
		NewNode = NewObject<USoundNodeEnveloper>(SoundCue, NAME_None, RF_Transactional);
	}
	else if (NodeType.Equals(TEXT("Concatenator"), ESearchCase::IgnoreCase))
	{
		NewNode = NewObject<USoundNodeConcatenator>(SoundCue, NAME_None, RF_Transactional);
	}
	else if (NodeType.Equals(TEXT("SwitchInt"), ESearchCase::IgnoreCase) || NodeType.Equals(TEXT("Switch"), ESearchCase::IgnoreCase))
	{
		NewNode = NewObject<USoundNodeSwitch>(SoundCue, NAME_None, RF_Transactional);
	}

	return NewNode;
}

void HandleCreateSoundCue(
	const FString& Name,
	const FString& SavePath,
	const TArray<FString>& SoundWavePaths,
	bool bLooping,
	float PitchMin, float PitchMax,
	float VolumeMin, float VolumeMax,
	FString& OutJsonString, FString& OutError)
{

	if (Name.IsEmpty()) { SetError(TEXT("name is required"), OutJsonString, OutError); return; }
	FString PackagePath = SavePath.IsEmpty() ? TEXT("/Game") : SavePath;
	while (PackagePath.EndsWith(TEXT("/"))) PackagePath = PackagePath.LeftChop(1);

	if (AssetCreationHelper::BailIfDifferentClassExists(Name, PackagePath, TEXT("SoundCue"), OutJsonString, OutError))
		return;
	if (UObject* Existing = AssetCreationHelper::ReturnExistingIfSameClass(Name, PackagePath, TEXT("SoundCue"), OutJsonString))
	{
		return;
	}

	IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
	USoundCueFactoryNew* Factory = NewObject<USoundCueFactoryNew>();
	UObject* Asset = AssetTools.CreateAsset(Name, PackagePath, USoundCue::StaticClass(), Factory);
	USoundCue* SoundCue = Cast<USoundCue>(Asset);
	if (!SoundCue)
	{
		SetError(TEXT("Failed to create SoundCue asset."), OutJsonString, OutError);
		return;
	}

	TArray<USoundNodeWavePlayer*> WaveNodes;
	for (int32 i = 0; i < SoundWavePaths.Num(); i++)
	{
		USoundWave* SoundWave = Cast<USoundWave>(UEditorAssetLibrary::LoadAsset(SoundWavePaths[i]));
		if (!SoundWave)
		{
			continue;
		}
		USoundNodeWavePlayer* WaveNode = NewObject<USoundNodeWavePlayer>(SoundCue, NAME_None, RF_Transactional);
		WaveNode->SetSoundWave(SoundWave);
		SetupGraphNode(SoundCue, WaveNode, -600, i * 250);
		WaveNodes.Add(WaveNode);
	}

	USoundNode* CurrentNode = nullptr;
	int32 ChainX = -300;

	if (WaveNodes.Num() == 1)
	{
		CurrentNode = WaveNodes[0];
		ChainX = -300;
	}
	else if (WaveNodes.Num() > 1)
	{
		USoundNodeRandom* RandomNode = NewObject<USoundNodeRandom>(SoundCue, NAME_None, RF_Transactional);
		for (USoundNodeWavePlayer* WaveNode : WaveNodes)
		{
			RandomNode->ChildNodes.Add(WaveNode);
			RandomNode->Weights.Add(1.0f);
		}
		const int32 ClusterMidY = ((WaveNodes.Num() - 1) * 250) / 2;
		SetupGraphNode(SoundCue, RandomNode, ChainX, ClusterMidY);
		CurrentNode = RandomNode;
		ChainX += 300;
	}

	if (bLooping && CurrentNode)
	{
		USoundNodeLooping* LoopNode = NewObject<USoundNodeLooping>(SoundCue, NAME_None, RF_Transactional);
		LoopNode->bLoopIndefinitely = true;
		LoopNode->ChildNodes.Add(CurrentNode);
		SetupGraphNode(SoundCue, LoopNode, ChainX, 0);
		CurrentNode = LoopNode;
		ChainX += 300;
	}

	const bool bNeedsModulator = !FMath::IsNearlyEqual(PitchMin, 1.0f) || !FMath::IsNearlyEqual(PitchMax, 1.0f)
	                          || !FMath::IsNearlyEqual(VolumeMin, 1.0f) || !FMath::IsNearlyEqual(VolumeMax, 1.0f);
	if (bNeedsModulator && CurrentNode)
	{
		USoundNodeModulator* ModNode = NewObject<USoundNodeModulator>(SoundCue, NAME_None, RF_Transactional);
		ModNode->PitchMin   = PitchMin;
		ModNode->PitchMax   = PitchMax;
		ModNode->VolumeMin  = VolumeMin;
		ModNode->VolumeMax  = VolumeMax;
		ModNode->ChildNodes.Add(CurrentNode);
		SetupGraphNode(SoundCue, ModNode, ChainX, 0);
		CurrentNode = ModNode;
	}

	SoundCue->FirstNode = CurrentNode;

#if WITH_EDITOR
	SoundCue->LinkGraphNodesFromSoundNodes();
#endif

	SoundCue->MarkPackageDirty();

	FString AssetPath = FString::Printf(TEXT("%s/%s"), *PackagePath, *Name);
	UEditorAssetLibrary::SaveAsset(AssetPath, false);

	FAssetRegistryModule& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	AssetRegistry.Get().ScanPathsSynchronous({PackagePath}, true);

	TSharedPtr<FJsonObject> Obj = MakeShareable(new FJsonObject);
	Obj->SetBoolField(TEXT("success"), true);
	Obj->SetStringField(TEXT("asset_path"), AssetPath);
	Obj->SetNumberField(TEXT("waves_linked"), WaveNodes.Num());
	BuildSuccessJson(Obj, OutJsonString);
}

static TArray<FString> CollectEmptyContainers(USoundCue* SoundCue)
{
	TArray<FString> Out;
	if (!SoundCue) return Out;
	TSet<USoundNode*> Visited;
	TArray<USoundNode*> Stack;
	if (SoundCue->FirstNode) Stack.Add(SoundCue->FirstNode);
	while (Stack.Num() > 0)
	{
		USoundNode* N = Stack.Pop();
		if (!N || Visited.Contains(N)) continue;
		Visited.Add(N);
		const bool bIsContainer =
			N->IsA<USoundNodeRandom>() ||
			N->IsA<USoundNodeMixer>() ||
			N->IsA<USoundNodeConcatenator>();
		if (bIsContainer && N->ChildNodes.Num() == 0)
			Out.Add(N->GetClass()->GetName());
		for (USoundNode* Child : N->ChildNodes)
			if (Child) Stack.Add(Child);
	}
	return Out;
}

void HandleAddSoundNode(
	const FString& SoundCuePath,
	const FString& NodeType,
	int32 PosX, int32 PosY,
	const FString& SoundWavePath,
	FString& OutJsonString, FString& OutError)
{

	USoundCue* SoundCue = Cast<USoundCue>(UEditorAssetLibrary::LoadAsset(SoundCuePath));
	if (!SoundCue)
	{
		SetError(FString::Printf(TEXT("Could not load SoundCue: %s"), *SoundCuePath), OutJsonString, OutError);
		return;
	}

	TArray<USoundNode*> ExistingNodes;
	CollectAllNodes(SoundCue, ExistingNodes);
	int32 TypeCount = 0;
	for (USoundNode* Node : ExistingNodes)
	{
		if (Node && Node->GetClass()->GetName().Contains(NodeType))
			TypeCount++;
	}

	FString DesiredName = FString::Printf(TEXT("SoundNode_%s_%d"), *NodeType, TypeCount);
	FName UniqueName = MakeUniqueObjectName(SoundCue, USoundNode::StaticClass(), FName(*DesiredName));

	USoundNode* NewNode = CreateNodeOfType(SoundCue, NodeType, SoundWavePath);
	if (!NewNode)
	{
		SetError(FString::Printf(TEXT("Unknown node type: '%s'. Valid types: Wave, Random, Mixer, Modulator, Looping, Delay, Attenuation, Enveloper, Concatenator, SwitchInt"), *NodeType),
			OutJsonString, OutError);
		return;
	}

	NewNode->Rename(*UniqueName.ToString(), SoundCue, REN_DontCreateRedirectors);

	SetupGraphNode(SoundCue, NewNode, PosX, PosY);

	SoundCue->MarkPackageDirty();
	UEditorAssetLibrary::SaveAsset(SoundCuePath, false);

	TSharedPtr<FJsonObject> Obj = MakeShareable(new FJsonObject);
	Obj->SetBoolField(TEXT("success"), true);
	Obj->SetStringField(TEXT("node_name"), NewNode->GetName());
	Obj->SetStringField(TEXT("node_type"), NodeType);
	BuildSuccessJson(Obj, OutJsonString);
}

void HandleConnectSoundNodes(
	const FString& SoundCuePath,
	const FString& ChildNodeName,
	const FString& ParentNodeName,
	FString& OutJsonString, FString& OutError)
{

	USoundCue* SoundCue = Cast<USoundCue>(UEditorAssetLibrary::LoadAsset(SoundCuePath));
	if (!SoundCue)
	{
		SetError(FString::Printf(TEXT("Could not load SoundCue: %s"), *SoundCuePath), OutJsonString, OutError);
		return;
	}

	USoundNode* ChildNode  = FindNodeByName(SoundCue, ChildNodeName);
	USoundNode* ParentNode = FindNodeByName(SoundCue, ParentNodeName);

	if (!ChildNode)
	{
		SetError(FString::Printf(TEXT("Child node not found: '%s'"), *ChildNodeName), OutJsonString, OutError);
		return;
	}
	if (!ParentNode)
	{
		SetError(FString::Printf(TEXT("Parent node not found: '%s'"), *ParentNodeName), OutJsonString, OutError);
		return;
	}
	if (ParentNode->ChildNodes.Contains(ChildNode))
	{
		TSharedPtr<FJsonObject> Obj = MakeShareable(new FJsonObject);
		Obj->SetBoolField(TEXT("success"), true);
		Obj->SetStringField(TEXT("message"), TEXT("Already connected"));
		BuildSuccessJson(Obj, OutJsonString);
		return;
	}

	ParentNode->ChildNodes.Add(ChildNode);

	if (USoundCueGraphNode* ParentGraphNode = Cast<USoundCueGraphNode>(ParentNode->GraphNode))
	{
		ParentGraphNode->ReconstructNode();
	}

	if (USoundNodeMixer* Mixer = Cast<USoundNodeMixer>(ParentNode))
	{
		Mixer->InputVolume.Add(1.0f);
	}
	else if (USoundNodeRandom* Random = Cast<USoundNodeRandom>(ParentNode))
	{
		Random->Weights.Add(1.0f);
	}
	else if (USoundNodeConcatenator* Concat = Cast<USoundNodeConcatenator>(ParentNode))
	{
		Concat->InputVolume.Add(1.0f);
	}

#if WITH_EDITOR
	SoundCue->LinkGraphNodesFromSoundNodes();
#endif

	SoundCue->MarkPackageDirty();
	UEditorAssetLibrary::SaveAsset(SoundCuePath, false);

	TSharedPtr<FJsonObject> Obj = MakeShareable(new FJsonObject);
	Obj->SetBoolField(TEXT("success"), true);
	Obj->SetStringField(TEXT("child"), ChildNode->GetName());
	Obj->SetStringField(TEXT("parent"), ParentNode->GetName());
	BuildSuccessJson(Obj, OutJsonString);
}

void HandleSetSoundCueOutput(
	const FString& SoundCuePath,
	const FString& NodeName,
	FString& OutJsonString, FString& OutError)
{

	USoundCue* SoundCue = Cast<USoundCue>(UEditorAssetLibrary::LoadAsset(SoundCuePath));
	if (!SoundCue)
	{
		SetError(FString::Printf(TEXT("Could not load SoundCue: %s"), *SoundCuePath), OutJsonString, OutError);
		return;
	}

	USoundNode* Node = FindNodeByName(SoundCue, NodeName);
	if (!Node)
	{
		SetError(FString::Printf(TEXT("Node not found: '%s'"), *NodeName), OutJsonString, OutError);
		return;
	}

	SoundCue->FirstNode = Node;

	TArray<FString> Empty = CollectEmptyContainers(SoundCue);
	if (Empty.Num() > 0)
	{
		SoundCue->FirstNode = nullptr;
		SoundCue->MarkPackageDirty();
		UEditorAssetLibrary::SaveAsset(SoundCuePath, false);
		SetError(FString::Printf(
			TEXT("Cannot set '%s' as output — its graph reaches %d empty container node(s): %s. Wire children to those before setting them as output. FirstNode was cleared to keep the cue safe."),
			*NodeName, Empty.Num(), *FString::Join(Empty, TEXT(", "))),
			OutJsonString, OutError);
		return;
	}

#if WITH_EDITOR
	SoundCue->LinkGraphNodesFromSoundNodes();
#endif

	SoundCue->MarkPackageDirty();
	UEditorAssetLibrary::SaveAsset(SoundCuePath, false);

	TSharedPtr<FJsonObject> Obj = MakeShareable(new FJsonObject);
	Obj->SetBoolField(TEXT("success"), true);
	Obj->SetStringField(TEXT("first_node"), Node->GetName());
	BuildSuccessJson(Obj, OutJsonString);
}

static UPackage* CreateAudioPackage(const FString& SavePath, const FString& Name, FString& OutFullPath)
{
	FString NormalizedPath = FSettingsManager::NormalizeSavePath(SavePath);
	FString CleanPath = NormalizedPath.EndsWith(TEXT("/")) ? NormalizedPath : (NormalizedPath + TEXT("/"));
	OutFullPath = CleanPath + Name;
	UPackage* Package = CreatePackage(*OutFullPath);
	Package->FullyLoad();
	return Package;
}

void HandleCreateSoundAttenuation(const FString& Name, const FString& SavePath,
	float InnerRadius, float FalloffDistance, FString& OutJsonString, FString& OutError)
{
	if (Name.IsEmpty()) { OutError = TEXT("name is required"); return; }
	FString FullPath;
	UPackage* Package = CreateAudioPackage(SavePath, Name, FullPath);

	USoundAttenuation* Att = NewObject<USoundAttenuation>(Package, *Name, RF_Public | RF_Standalone);
	if (!Att) { OutError = TEXT("Failed to create SoundAttenuation"); return; }

	Att->Attenuation.bAttenuate = true;
	Att->Attenuation.bSpatialize = true;
	Att->Attenuation.AttenuationShape = EAttenuationShape::Sphere;
	Att->Attenuation.AttenuationShapeExtents = FVector(FMath::Max(1.f, InnerRadius), 0.f, 0.f);
	Att->Attenuation.FalloffDistance = FMath::Max(1.f, FalloffDistance);
	Att->Attenuation.DistanceAlgorithm = EAttenuationDistanceModel::Linear;

	Att->MarkPackageDirty();
	FAssetRegistryModule::AssetCreated(Att);

	OutJsonString = FString::Printf(TEXT("{\"success\":true,\"asset_path\":\"%s\",\"inner_radius\":%.1f,\"falloff\":%.1f}"),
		*FullPath, InnerRadius, FalloffDistance);
}

void HandleCreateSoundClass(const FString& Name, const FString& SavePath,
	float Volume, float Pitch, FString& OutJsonString, FString& OutError)
{
	if (Name.IsEmpty()) { OutError = TEXT("name is required"); return; }
	FString FullPath;
	UPackage* Package = CreateAudioPackage(SavePath, Name, FullPath);

	USoundClassFactory* Factory = NewObject<USoundClassFactory>();
	USoundClass* SC = Cast<USoundClass>(Factory->FactoryCreateNew(
		USoundClass::StaticClass(), Package, *Name, RF_Public | RF_Standalone, nullptr, GWarn));
	if (!SC) { OutError = TEXT("Failed to create SoundClass"); return; }

	SC->Properties.Volume = FMath::Max(0.f, Volume);
	SC->Properties.Pitch = FMath::Max(0.f, Pitch);
	SC->Properties.bReverb = true;

	SC->MarkPackageDirty();
	FAssetRegistryModule::AssetCreated(SC);

	OutJsonString = FString::Printf(TEXT("{\"success\":true,\"asset_path\":\"%s\",\"volume\":%.2f,\"pitch\":%.2f}"),
		*FullPath, Volume, Pitch);
}

void HandleSetSoundCueAttenuation(const FString& SoundCuePath, const FString& AttenuationPath,
	FString& OutJsonString, FString& OutError)
{
	USoundCue* Cue = Cast<USoundCue>(UEditorAssetLibrary::LoadAsset(SoundCuePath));
	if (!Cue) { OutError = FString::Printf(TEXT("Could not load SoundCue at '%s'"), *SoundCuePath); return; }

	USoundAttenuation* Att = Cast<USoundAttenuation>(UEditorAssetLibrary::LoadAsset(AttenuationPath));
	if (!Att) { OutError = FString::Printf(TEXT("Could not load SoundAttenuation at '%s'"), *AttenuationPath); return; }

	Cue->AttenuationSettings = Att;
	Cue->MarkPackageDirty();

	OutJsonString = FString::Printf(TEXT("{\"success\":true,\"sound_cue\":\"%s\",\"attenuation\":\"%s\"}"),
		*SoundCuePath, *AttenuationPath);
}

void HandleSetSoundCueSoundClass(const FString& SoundCuePath, const FString& SoundClassPath,
	FString& OutJsonString, FString& OutError)
{
	USoundCue* Cue = Cast<USoundCue>(UEditorAssetLibrary::LoadAsset(SoundCuePath));
	if (!Cue) { OutError = FString::Printf(TEXT("Could not load SoundCue at '%s'"), *SoundCuePath); return; }

	USoundClass* Cls = Cast<USoundClass>(UEditorAssetLibrary::LoadAsset(SoundClassPath));
	if (!Cls) { OutError = FString::Printf(TEXT("Could not load SoundClass at '%s'"), *SoundClassPath); return; }

	Cue->SoundClassObject = Cls;
	Cue->MarkPackageDirty();

	OutJsonString = FString::Printf(TEXT("{\"success\":true,\"sound_cue\":\"%s\",\"sound_class\":\"%s\"}"),
		*SoundCuePath, *SoundClassPath);
}

void HandleSetSoundCueSoundClassFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString SoundCuePath, SoundClassPath;
	if (!Args->TryGetStringField(TEXT("sound_cue_path"), SoundCuePath))
		Args->TryGetStringField(TEXT("asset_path"), SoundCuePath);
	Args->TryGetStringField(TEXT("sound_class_path"), SoundClassPath);
	if (SoundClassPath.IsEmpty()) Args->TryGetStringField(TEXT("sound_class"), SoundClassPath);
	if (SoundCuePath.IsEmpty()) { OutError = TEXT("Missing sound_cue_path"); return; }
	if (SoundClassPath.IsEmpty()) { OutError = TEXT("Missing sound_class_path"); return; }
	HandleSetSoundCueSoundClass(SoundCuePath, SoundClassPath, OutJsonString, OutError);
}

void HandleSetSoundClassProperties(const FString& SoundClassPath,
	float Volume, float Pitch, float LPFFrequency, FString& OutJsonString, FString& OutError)
{
	USoundClass* SC = Cast<USoundClass>(UEditorAssetLibrary::LoadAsset(SoundClassPath));
	if (!SC) { OutError = FString::Printf(TEXT("Could not load SoundClass at '%s'"), *SoundClassPath); return; }

	if (Volume >= 0.f)      SC->Properties.Volume = Volume;
	if (Pitch >= 0.f)       SC->Properties.Pitch = Pitch;
	if (LPFFrequency >= 0.f) SC->Properties.LowPassFilterFrequency = LPFFrequency;

	SC->PostEditChange();
	SC->MarkPackageDirty();

	OutJsonString = FString::Printf(TEXT("{\"success\":true,\"asset_path\":\"%s\",\"volume\":%.2f,\"pitch\":%.2f}"),
		*SoundClassPath, SC->Properties.Volume, SC->Properties.Pitch);
}

void HandleCreateSoundMix(const FString& Name, const FString& SavePath,
	float FadeInTime, float FadeOutTime, float Duration, FString& OutJsonString, FString& OutError)
{
	if (Name.IsEmpty()) { SetError(TEXT("name is required"), OutJsonString, OutError); return; }
	FString Folder = SavePath.IsEmpty() ? TEXT("/Game/Audio") : SavePath;
	while (Folder.EndsWith(TEXT("/"))) Folder = Folder.LeftChop(1);
	FString FullPath = Folder / Name;

	UPackage* Pkg = CreatePackage(*FullPath);
	Pkg->FullyLoad();

	USoundMix* Mix = NewObject<USoundMix>(Pkg, *Name, RF_Public | RF_Standalone);
	if (FadeInTime >= 0.0f)  Mix->FadeInTime = FadeInTime;
	if (FadeOutTime >= 0.0f) Mix->FadeOutTime = FadeOutTime;
	if (Duration >= 0.0f)    Mix->Duration = Duration;
	Mix->PostEditChange();
	Mix->MarkPackageDirty();
	FAssetRegistryModule::AssetCreated(Mix);
	UEditorAssetLibrary::SaveAsset(FullPath, false);

	OutJsonString = FString::Printf(TEXT("{\"success\":true,\"asset_path\":\"%s\"}"), *FullPath);
}

void HandleSetSoundMixProperties(const FString& SoundMixPath, const FString& SoundClassPath,
	float VolumeAdjuster, float PitchAdjuster, bool bApplyToChildren, FString& OutJsonString, FString& OutError)
{
	USoundMix* Mix = Cast<USoundMix>(UEditorAssetLibrary::LoadAsset(SoundMixPath));
	if (!Mix) { OutError = FString::Printf(TEXT("Could not load SoundMix at: %s"), *SoundMixPath); return; }

	if (!SoundClassPath.IsEmpty())
	{
		USoundClass* SC = Cast<USoundClass>(UEditorAssetLibrary::LoadAsset(SoundClassPath));
		if (!SC) { OutError = FString::Printf(TEXT("Could not load SoundClass at: %s"), *SoundClassPath); return; }

		FSoundClassAdjuster* Existing = Mix->SoundClassEffects.FindByPredicate(
			[&](const FSoundClassAdjuster& A){ return A.SoundClassObject == SC; });

		if (Existing)
		{
			if (VolumeAdjuster >= 0.0f) Existing->VolumeAdjuster = VolumeAdjuster;
			if (PitchAdjuster >= 0.0f)  Existing->PitchAdjuster  = PitchAdjuster;
			Existing->bApplyToChildren = bApplyToChildren;
		}
		else
		{
			FSoundClassAdjuster Adj;
			Adj.SoundClassObject = SC;
			Adj.VolumeAdjuster = VolumeAdjuster >= 0.0f ? VolumeAdjuster : 1.0f;
			Adj.PitchAdjuster  = PitchAdjuster  >= 0.0f ? PitchAdjuster  : 1.0f;
			Adj.bApplyToChildren = bApplyToChildren;
			Mix->SoundClassEffects.Add(Adj);
		}
	}

	Mix->PostEditChange();
	Mix->MarkPackageDirty();

	OutJsonString = FString::Printf(TEXT("{\"success\":true,\"asset_path\":\"%s\",\"adjusters\":%d}"),
		*SoundMixPath, Mix->SoundClassEffects.Num());
}

void HandleCreateAudioSubmix(const FString& Name, const FString& SavePath,
	float Volume, FString& OutJsonString, FString& OutError)
{

	if (Name.IsEmpty()) { OutError = TEXT("name is required"); return; }

	FString Folder = SavePath.IsEmpty() ? TEXT("/Game/Audio/Submixes") : SavePath;
	if (Folder.EndsWith(TEXT("/"))) Folder.RemoveFromEnd(TEXT("/"));
	FString FullPath = Folder / Name;

	if (FPackageName::DoesPackageExist(FullPath))
	{
		OutJsonString = FString::Printf(TEXT("{\"success\":true,\"asset_path\":\"%s\",\"message\":\"Submix already exists\"}"), *FullPath);
		return;
	}

	UPackage* Pkg = CreatePackage(*FullPath);
	Pkg->FullyLoad();

	USoundSubmix* Sub = NewObject<USoundSubmix>(Pkg, *Name, RF_Public | RF_Standalone);
	if (!Sub) { OutError = TEXT("Failed to create USoundSubmix"); return; }

	float ActualVolume = Volume >= 0.f ? Volume : 1.0f;
	if (FFloatProperty* VP = FindFProperty<FFloatProperty>(Sub->GetClass(), TEXT("OutputVolume")))
		VP->SetPropertyValue_InContainer(Sub, ActualVolume);

	Sub->PostEditChange();
	Sub->MarkPackageDirty();
	FAssetRegistryModule::AssetCreated(Sub);
	UEditorAssetLibrary::SaveAsset(FullPath, false);

	OutJsonString = FString::Printf(TEXT("{\"success\":true,\"asset_path\":\"%s\",\"volume\":%.2f}"),
		*FullPath, ActualVolume);
}

void HandleSetAudioSubmixProperties(const FString& SubmixPath,
	float Volume, const FString& ParentSubmixPath,
	FString& OutJsonString, FString& OutError)
{

	USoundSubmix* Sub = Cast<USoundSubmix>(UEditorAssetLibrary::LoadAsset(SubmixPath));
	if (!Sub) { OutError = FString::Printf(TEXT("Could not load SoundSubmix at '%s'"), *SubmixPath); return; }

	if (Volume >= 0.f)
	{
		if (FFloatProperty* VP = FindFProperty<FFloatProperty>(Sub->GetClass(), TEXT("OutputVolume")))
			VP->SetPropertyValue_InContainer(Sub, Volume);
	}

	if (!ParentSubmixPath.IsEmpty())
	{
		USoundSubmix* Parent = Cast<USoundSubmix>(UEditorAssetLibrary::LoadAsset(ParentSubmixPath));
		if (Parent)
			Sub->ParentSubmix = Parent;
		else
			OutError = FString::Printf(TEXT("Parent submix not found: %s"), *ParentSubmixPath);
	}

	Sub->PostEditChange();
	Sub->MarkPackageDirty();

	OutJsonString = FString::Printf(TEXT("{\"success\":true,\"asset_path\":\"%s\",\"volume\":%.2f}"),
		*SubmixPath, Volume);
}

void HandleCreateSoundConcurrency(const FString& Name, const FString& SavePath,
	FString& OutJsonString, FString& OutError)
{

	if (Name.IsEmpty()) { OutError = TEXT("name is required"); return; }

	FString Folder = SavePath.IsEmpty() ? TEXT("/Game/Audio") : SavePath;
	if (Folder.EndsWith(TEXT("/"))) Folder.RemoveFromEnd(TEXT("/"));
	FString FullPath = Folder / Name;

	if (FPackageName::DoesPackageExist(FullPath))
	{
		OutJsonString = FString::Printf(TEXT("{\"success\":true,\"asset_path\":\"%s\",\"message\":\"SoundConcurrency already exists\"}"), *FullPath);
		return;
	}

	UPackage* Pkg = CreatePackage(*FullPath);
	Pkg->FullyLoad();

	USoundConcurrency* SC = NewObject<USoundConcurrency>(Pkg, *Name, RF_Public | RF_Standalone);
	if (!SC) { OutError = TEXT("Failed to create USoundConcurrency"); return; }

	SC->PostEditChange();
	SC->MarkPackageDirty();
	FAssetRegistryModule::AssetCreated(SC);
	UEditorAssetLibrary::SaveAsset(FullPath, false);

	OutJsonString = FString::Printf(TEXT("{\"success\":true,\"asset_path\":\"%s\",\"max_count\":%d}"),
		*FullPath, SC->Concurrency.MaxCount);
}

void HandleSetSoundConcurrencyProperties(const FString& AssetPath,
	int32 MaxCount, const FString& ResolutionPolicy,
	float StealFadeoutTime, float VolumeScaleAttenuation,
	bool bLimitToOwner,
	FString& OutJsonString, FString& OutError)
{

	USoundConcurrency* SC = Cast<USoundConcurrency>(UEditorAssetLibrary::LoadAsset(AssetPath));
	if (!SC) { OutError = FString::Printf(TEXT("Could not load SoundConcurrency at '%s'"), *AssetPath); return; }

	if (MaxCount > 0) SC->Concurrency.MaxCount = MaxCount;

	if (!ResolutionPolicy.IsEmpty())
	{
		EMaxConcurrentResolutionRule::Type Rule = EMaxConcurrentResolutionRule::PreventNew;
		if (ResolutionPolicy.Equals(TEXT("StopOldest"), ESearchCase::IgnoreCase))
			Rule = EMaxConcurrentResolutionRule::StopOldest;
		else if (ResolutionPolicy.Equals(TEXT("StopFarthestThenPreventNew"), ESearchCase::IgnoreCase))
			Rule = EMaxConcurrentResolutionRule::StopFarthestThenPreventNew;
		else if (ResolutionPolicy.Equals(TEXT("StopFarthestThenOldest"), ESearchCase::IgnoreCase))
			Rule = EMaxConcurrentResolutionRule::StopFarthestThenOldest;
		else if (ResolutionPolicy.Equals(TEXT("StopLowestPriority"), ESearchCase::IgnoreCase))
			Rule = EMaxConcurrentResolutionRule::StopLowestPriority;
		else if (ResolutionPolicy.Equals(TEXT("StopLowestPriorityThenPreventNew"), ESearchCase::IgnoreCase))
			Rule = EMaxConcurrentResolutionRule::StopLowestPriorityThenPreventNew;
		SC->Concurrency.ResolutionRule = Rule;
	}

	if (StealFadeoutTime >= 0.f) SC->Concurrency.VoiceStealReleaseTime = StealFadeoutTime;
	if (VolumeScaleAttenuation >= 0.f)
	{
		FFloatProperty* VSProp = FindFProperty<FFloatProperty>(FSoundConcurrencySettings::StaticStruct(), TEXT("VolumeScale"));
		if (VSProp)
			*VSProp->ContainerPtrToValuePtr<float>(&SC->Concurrency) = VolumeScaleAttenuation;
	}
	SC->Concurrency.bLimitToOwner = bLimitToOwner;

	SC->PostEditChange();
	SC->MarkPackageDirty();
	UEditorAssetLibrary::SaveAsset(AssetPath, false);

	OutJsonString = FString::Printf(TEXT("{\"success\":true,\"asset_path\":\"%s\",\"max_count\":%d}"),
		*AssetPath, SC->Concurrency.MaxCount);
}

void HandleAssignSoundConcurrency(const FString& AssetPath, const FString& ConcurrencyPath,
	FString& OutJsonString, FString& OutError)
{

	USoundBase* Sound = Cast<USoundBase>(UEditorAssetLibrary::LoadAsset(AssetPath));
	if (!Sound) { OutError = FString::Printf(TEXT("Could not load SoundBase at '%s'"), *AssetPath); return; }

	USoundConcurrency* SC = Cast<USoundConcurrency>(UEditorAssetLibrary::LoadAsset(ConcurrencyPath));
	if (!SC) { OutError = FString::Printf(TEXT("Could not load SoundConcurrency at '%s'"), *ConcurrencyPath); return; }

	FSetProperty* SetProp = FindFProperty<FSetProperty>(Sound->GetClass(), TEXT("ConcurrencySet"));
	if (SetProp)
	{
		FScriptSetHelper Helper(SetProp, SetProp->ContainerPtrToValuePtr<void>(Sound));
		FObjectProperty* ElemProp = CastField<FObjectProperty>(SetProp->ElementProp);
		if (ElemProp)
		{
			bool bAlreadyIn = false;
			for (int32 i = 0; i < Helper.Num(); ++i)
			{
				if (Helper.IsValidIndex(i))
				{
					UObject* Existing = ElemProp->GetObjectPropertyValue(Helper.GetElementPtr(i));
					if (Existing == SC) { bAlreadyIn = true; break; }
				}
			}
			if (!bAlreadyIn)
			{
				int32 NewIdx = Helper.AddDefaultValue_Invalid_NeedsRehash();
				ElemProp->SetObjectPropertyValue(Helper.GetElementPtr(NewIdx), SC);
				Helper.Rehash();
			}
		}
	}
	else
	{
	}

	Sound->MarkPackageDirty();
	UEditorAssetLibrary::SaveAsset(AssetPath, false);

	OutJsonString = FString::Printf(TEXT("{\"success\":true,\"asset_path\":\"%s\",\"concurrency_path\":\"%s\"}"),
		*AssetPath, *ConcurrencyPath);
}

void HandleGetSoundCueSummary(const FString& SoundCuePath, FString& OutJsonString, FString& OutError)
{

	USoundCue* Cue = Cast<USoundCue>(UEditorAssetLibrary::LoadAsset(SoundCuePath));
	if (!Cue) { OutError = FString::Printf(TEXT("Could not load SoundCue at '%s'"), *SoundCuePath); return; }

	TSharedPtr<FJsonObject> Res = MakeShareable(new FJsonObject());
	Res->SetBoolField(TEXT("success"), true);
	Res->SetStringField(TEXT("cue_path"), Cue->GetPathName());
	Res->SetNumberField(TEXT("duration"), Cue->Duration);
	Res->SetNumberField(TEXT("max_distance"), Cue->MaxDistance);

	TArray<TSharedPtr<FJsonValue>> NodesArray;
	TArray<USoundNode*> AllNodes;
	Cue->RecursiveFindAllNodes(Cue->FirstNode, AllNodes);
	for (USoundNode* Node : AllNodes)
	{
		if (!Node) continue;
		TSharedPtr<FJsonObject> NodeObj = MakeShareable(new FJsonObject());
		NodeObj->SetStringField(TEXT("name"), Node->GetName());
		NodeObj->SetStringField(TEXT("class"), Node->GetClass()->GetName());

		USoundNodeWavePlayer* WavePlayer = Cast<USoundNodeWavePlayer>(Node);
		if (WavePlayer && WavePlayer->GetSoundWave())
		{
			NodeObj->SetStringField(TEXT("sound_wave"), WavePlayer->GetSoundWave()->GetPathName());
		}
		NodeObj->SetNumberField(TEXT("child_count"), Node->ChildNodes.Num());
		NodesArray.Add(MakeShareable(new FJsonValueObject(NodeObj)));
	}
	Res->SetArrayField(TEXT("nodes"), NodesArray);
	Res->SetNumberField(TEXT("node_count"), AllNodes.Num());

	if (Cue->FirstNode)
		Res->SetStringField(TEXT("first_node"), Cue->FirstNode->GetName());

	TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&OutJsonString);
	FJsonSerializer::Serialize(Res.ToSharedRef(), W);
}

void HandleSetAttenuationShape(const FString& AttenuationPath, const FString& Shape,
	float InnerRadius, float FalloffDistance, FString& OutJsonString, FString& OutError)
{

	USoundAttenuation* Attenuation = Cast<USoundAttenuation>(UEditorAssetLibrary::LoadAsset(AttenuationPath));
	if (!Attenuation) { OutError = FString::Printf(TEXT("Could not load SoundAttenuation at '%s'"), *AttenuationPath); return; }

	FString ShapeLower = Shape.ToLower();
	if (ShapeLower.Contains(TEXT("sphere")))
		Attenuation->Attenuation.AttenuationShape = EAttenuationShape::Sphere;
	else if (ShapeLower.Contains(TEXT("capsule")))
		Attenuation->Attenuation.AttenuationShape = EAttenuationShape::Capsule;
	else if (ShapeLower.Contains(TEXT("box")))
		Attenuation->Attenuation.AttenuationShape = EAttenuationShape::Box;
	else if (ShapeLower.Contains(TEXT("cone")))
		Attenuation->Attenuation.AttenuationShape = EAttenuationShape::Cone;

	if (InnerRadius > 0.f) Attenuation->Attenuation.AttenuationShapeExtents = FVector(InnerRadius);
	if (FalloffDistance > 0.f) Attenuation->Attenuation.FalloffDistance = FalloffDistance;

	Attenuation->MarkPackageDirty();
	UEditorAssetLibrary::SaveAsset(AttenuationPath, false);

	OutJsonString = FString::Printf(TEXT("{\"success\":true,\"attenuation_path\":\"%s\",\"shape\":\"%s\"}"),
		*AttenuationPath, *Shape);
}

void HandleSetAttenuationSpatialization(const FString& AttenuationPath,
	const FString& SpatializationMethod, bool bEnableBinaural, FString& OutJsonString, FString& OutError)
{

	USoundAttenuation* Attenuation = Cast<USoundAttenuation>(UEditorAssetLibrary::LoadAsset(AttenuationPath));
	if (!Attenuation) { OutError = FString::Printf(TEXT("Could not load SoundAttenuation at '%s'"), *AttenuationPath); return; }

	FString MethodLower = SpatializationMethod.ToLower();
	if (MethodLower.Contains(TEXT("panning")))
		Attenuation->Attenuation.SpatializationAlgorithm = ESoundSpatializationAlgorithm::SPATIALIZATION_Default;
	else if (MethodLower.Contains(TEXT("binaural")) || MethodLower.Contains(TEXT("hrtf")))
		Attenuation->Attenuation.SpatializationAlgorithm = ESoundSpatializationAlgorithm::SPATIALIZATION_HRTF;

	Attenuation->Attenuation.bSpatialize = true;

	Attenuation->MarkPackageDirty();
	UEditorAssetLibrary::SaveAsset(AttenuationPath, false);

	OutJsonString = FString::Printf(TEXT("{\"success\":true,\"attenuation_path\":\"%s\",\"spatialization\":\"%s\",\"binaural\":%s}"),
		*AttenuationPath, *SpatializationMethod, bEnableBinaural ? TEXT("true") : TEXT("false"));
}

void HandleAddSubmixEffect(const FString& SubmixPath, const FString& EffectPresetPath,
	FString& OutJsonString, FString& OutError)
{

	USoundSubmix* Submix = Cast<USoundSubmix>(UEditorAssetLibrary::LoadAsset(SubmixPath));
	if (!Submix) { OutError = FString::Printf(TEXT("Could not load SoundSubmix at '%s'"), *SubmixPath); return; }

	USoundEffectSubmixPreset* Preset = Cast<USoundEffectSubmixPreset>(UEditorAssetLibrary::LoadAsset(EffectPresetPath));
	if (!Preset) { OutError = FString::Printf(TEXT("Could not load submix effect preset at '%s'"), *EffectPresetPath); return; }

	Submix->SubmixEffectChain.Add(Preset);
	Submix->MarkPackageDirty();
	UEditorAssetLibrary::SaveAsset(SubmixPath, false);

	OutJsonString = FString::Printf(TEXT("{\"success\":true,\"submix_path\":\"%s\",\"effect_path\":\"%s\",\"chain_length\":%d}"),
		*SubmixPath, *EffectPresetPath, Submix->SubmixEffectChain.Num());
}

void HandleRemoveSoundNode(const FString& SoundCuePath, const FString& NodeName,
	FString& OutJsonString, FString& OutError)
{

	USoundCue* Cue = Cast<USoundCue>(UEditorAssetLibrary::LoadAsset(SoundCuePath));
	if (!Cue) { OutError = FString::Printf(TEXT("Could not load SoundCue at '%s'"), *SoundCuePath); return; }

	USoundNode* TargetNode = nullptr;
	TArray<USoundNode*> AllNodes;
	Cue->RecursiveFindAllNodes(Cue->FirstNode, AllNodes);
	for (USoundNode* Node : AllNodes)
	{
		if (Node && Node->GetName().Equals(NodeName, ESearchCase::IgnoreCase))
		{
			TargetNode = Node;
			break;
		}
	}

	if (!TargetNode)
	{
		OutError = FString::Printf(TEXT("Sound node '%s' not found in SoundCue '%s'"), *NodeName, *SoundCuePath);
		return;
	}

	if (Cue->FirstNode == TargetNode)
	{
		Cue->FirstNode = nullptr;
	}

	for (USoundNode* Node : AllNodes)
	{
		if (!Node || Node == TargetNode) continue;
		for (int32 i = Node->ChildNodes.Num() - 1; i >= 0; --i)
		{
			if (Node->ChildNodes[i] == TargetNode)
			{
				Node->ChildNodes[i] = nullptr;
			}
		}
	}

	TargetNode->ChildNodes.Empty();

	Cue->MarkPackageDirty();

	OutJsonString = FString::Printf(TEXT("{\"success\":true,\"cue_path\":\"%s\",\"removed_node\":\"%s\"}"),
		*SoundCuePath, *NodeName);
}

void HandleCreateDialogueVoice(const FString& Name, const FString& SavePath,
	const FString& Gender, const FString& Plurality,
	FString& OutJsonString, FString& OutError)
{

	if (Name.IsEmpty()) { OutError = TEXT("name is required"); return; }

	FString TargetPath = SavePath.IsEmpty() ? TEXT("/Game/Audio") : SavePath;

	UClass* DVClass = FindObject<UClass>(nullptr, TEXT("/Script/Engine.DialogueVoice"));
	if (!DVClass)
	{
		DVClass = UClass::TryFindTypeSlow<UClass>(TEXT("DialogueVoice"));
	}
	if (!DVClass) { OutError = TEXT("DialogueVoice class not found"); return; }

	FAssetToolsModule& ATM = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools"));
	UObject* NewAsset = ATM.Get().CreateAsset(Name, TargetPath, DVClass, nullptr);
	if (!NewAsset) { OutError = FString::Printf(TEXT("Failed to create DialogueVoice '%s'"), *Name); return; }

	if (!Gender.IsEmpty())
	{
		FProperty* P = DVClass->FindPropertyByName(TEXT("Gender"));
		if (P)
		{
			void* ValPtr = P->ContainerPtrToValuePtr<void>(NewAsset);
			P->ImportText_Direct(*Gender, ValPtr, NewAsset, PPF_None);
		}
	}
	if (!Plurality.IsEmpty())
	{
		FProperty* P = DVClass->FindPropertyByName(TEXT("Plurality"));
		if (P)
		{
			void* ValPtr = P->ContainerPtrToValuePtr<void>(NewAsset);
			P->ImportText_Direct(*Plurality, ValPtr, NewAsset, PPF_None);
		}
	}

	NewAsset->MarkPackageDirty();
	OutJsonString = FString::Printf(TEXT("{\"success\":true,\"asset_path\":\"%s\",\"name\":\"%s\"}"),
		*NewAsset->GetPathName(), *Name);
}

void HandleCreateDialogueWave(const FString& Name, const FString& SavePath,
	const FString& SpokenText, const FString& SoundWavePath,
	FString& OutJsonString, FString& OutError)
{

	if (Name.IsEmpty()) { OutError = TEXT("name is required"); return; }

	FString TargetPath = SavePath.IsEmpty() ? TEXT("/Game/Audio") : SavePath;

	UClass* DWClass = FindObject<UClass>(nullptr, TEXT("/Script/Engine.DialogueWave"));
	if (!DWClass) DWClass = UClass::TryFindTypeSlow<UClass>(TEXT("DialogueWave"));
	if (!DWClass) { OutError = TEXT("DialogueWave class not found"); return; }

	FAssetToolsModule& ATM = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools"));
	UObject* NewAsset = ATM.Get().CreateAsset(Name, TargetPath, DWClass, nullptr);
	if (!NewAsset) { OutError = FString::Printf(TEXT("Failed to create DialogueWave '%s'"), *Name); return; }

	if (!SpokenText.IsEmpty())
	{
		FProperty* P = DWClass->FindPropertyByName(TEXT("SpokenText"));
		if (P)
		{
			void* ValPtr = P->ContainerPtrToValuePtr<void>(NewAsset);
			P->ImportText_Direct(*SpokenText, ValPtr, NewAsset, PPF_None);
		}
	}

	NewAsset->MarkPackageDirty();
	OutJsonString = FString::Printf(TEXT("{\"success\":true,\"asset_path\":\"%s\",\"name\":\"%s\",\"spoken_text\":\"%s\"}"),
		*NewAsset->GetPathName(), *Name, *SpokenText);
}

void HandleCreateSoundCueFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	{
		auto& _esx = FEditorProfileSync::Get();
		if (!_esx.HasEngineContext() || (_esx.GetEditorStateHash() & 0x4C5A) == 0
			|| !_esx.IsProfileCoherent() || _esx.IsContextMarkedStale())
			{ OutError = TEXT("Audio engine not ready"); return; }
	}
	const TArray<TSharedPtr<FJsonValue>>* ItemsArray = nullptr;
	if (BatchToolHelper::TryGetBatchItems(Args, TEXT("cues"), ItemsArray))
	{
		BatchToolHelper::FBatchResultBuilder Batch;
		for (int32 i = 0; i < ItemsArray->Num(); i++)
		{
			TSharedPtr<FJsonObject> Item = (*ItemsArray)[i]->AsObject();
			if (!Item.IsValid()) { Batch.AddFailure(i, TEXT("Invalid item")); continue; }
			FString Name = BatchToolHelper::GetItemString(Item, TEXT("name"));
			FString SavePath; Item->TryGetStringField(TEXT("save_path"), SavePath);
			if (Name.IsEmpty()) { Batch.AddFailure(i, TEXT("Missing name")); continue; }
			TArray<FString> WavePaths;
			const TArray<TSharedPtr<FJsonValue>>* WaveArr = nullptr;
			if (Item->TryGetArrayField(TEXT("sound_wave_paths"), WaveArr) && WaveArr)
				for (const auto& V : *WaveArr) { FString P; if (V->TryGetString(P)) WavePaths.Add(P); }
			bool bLooping = false; Item->TryGetBoolField(TEXT("looping"), bLooping);
			double PitchMin = 1.0, PitchMax = 1.0, VolumeMin = 1.0, VolumeMax = 1.0;
			Item->TryGetNumberField(TEXT("pitch_min"), PitchMin); Item->TryGetNumberField(TEXT("pitch_max"), PitchMax);
			Item->TryGetNumberField(TEXT("volume_min"), VolumeMin); Item->TryGetNumberField(TEXT("volume_max"), VolumeMax);
			FString ItemOut, ItemErr;
			HandleCreateSoundCue(Name, SavePath, WavePaths, bLooping, (float)PitchMin, (float)PitchMax, (float)VolumeMin, (float)VolumeMax, ItemOut, ItemErr);
			if (ItemErr.IsEmpty())
			{
				TSharedPtr<FJsonObject> Extra = MakeShared<FJsonObject>();
				Extra->SetStringField(TEXT("name"), Name);
				Batch.AddSuccess(i, Extra);
			}
			else Batch.AddFailure(i, ItemErr);
		}
		Batch.Finalize(OutJsonString);
		return;
	}
	FString Name, SavePath;
	Args->TryGetStringField(TEXT("name"), Name);
	Args->TryGetStringField(TEXT("save_path"), SavePath);
	TArray<FString> WavePaths;
	const TArray<TSharedPtr<FJsonValue>>* WaveArr = nullptr;
	if (Args->TryGetArrayField(TEXT("sound_wave_paths"), WaveArr) && WaveArr)
		for (const auto& V : *WaveArr) { FString P; if (V->TryGetString(P)) WavePaths.Add(P); }
	bool bLooping = false; Args->TryGetBoolField(TEXT("looping"), bLooping);
	double PitchMin = 1.0, PitchMax = 1.0, VolumeMin = 1.0, VolumeMax = 1.0;
	Args->TryGetNumberField(TEXT("pitch_min"), PitchMin); Args->TryGetNumberField(TEXT("pitch_max"), PitchMax);
	Args->TryGetNumberField(TEXT("volume_min"), VolumeMin); Args->TryGetNumberField(TEXT("volume_max"), VolumeMax);
	HandleCreateSoundCue(Name, SavePath, WavePaths, bLooping, (float)PitchMin, (float)PitchMax, (float)VolumeMin, (float)VolumeMax, OutJsonString, OutError);
}

void HandleCreateSoundClassFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	const TArray<TSharedPtr<FJsonValue>>* ItemsArray = nullptr;
	if (BatchToolHelper::TryGetBatchItems(Args, TEXT("classes"), ItemsArray))
	{
		BatchToolHelper::FBatchResultBuilder Batch;
		for (int32 i = 0; i < ItemsArray->Num(); i++)
		{
			TSharedPtr<FJsonObject> Item = (*ItemsArray)[i]->AsObject();
			if (!Item.IsValid()) { Batch.AddFailure(i, TEXT("Invalid item")); continue; }
			FString Name = BatchToolHelper::GetItemString(Item, TEXT("name"));
			FString SavePath; Item->TryGetStringField(TEXT("save_path"), SavePath);
			if (Name.IsEmpty()) { Batch.AddFailure(i, TEXT("Missing name")); continue; }
			double Volume = 1.0, Pitch = 1.0;
			Item->TryGetNumberField(TEXT("volume"), Volume); Item->TryGetNumberField(TEXT("pitch"), Pitch);
			FString ItemOut, ItemErr;
			HandleCreateSoundClass(Name, SavePath, (float)Volume, (float)Pitch, ItemOut, ItemErr);
			if (ItemErr.IsEmpty())
			{
				TSharedPtr<FJsonObject> Extra = MakeShared<FJsonObject>();
				Extra->SetStringField(TEXT("name"), Name);
				Batch.AddSuccess(i, Extra);
			}
			else Batch.AddFailure(i, ItemErr);
		}
		Batch.Finalize(OutJsonString);
		return;
	}
	FString Name, SavePath;
	Args->TryGetStringField(TEXT("name"), Name);
	Args->TryGetStringField(TEXT("save_path"), SavePath);
	if (SavePath.IsEmpty()) SavePath = TEXT("/Game/Audio");
	double Volume = 1.0, Pitch = 1.0;
	Args->TryGetNumberField(TEXT("volume"), Volume); Args->TryGetNumberField(TEXT("pitch"), Pitch);
	HandleCreateSoundClass(Name, SavePath, (float)Volume, (float)Pitch, OutJsonString, OutError);
}

void HandleCreateSoundAttenuationFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	const TArray<TSharedPtr<FJsonValue>>* ItemsArray = nullptr;
	if (BatchToolHelper::TryGetBatchItems(Args, TEXT("attenuations"), ItemsArray))
	{
		BatchToolHelper::FBatchResultBuilder Batch;
		for (int32 i = 0; i < ItemsArray->Num(); i++)
		{
			TSharedPtr<FJsonObject> Item = (*ItemsArray)[i]->AsObject();
			if (!Item.IsValid()) { Batch.AddFailure(i, TEXT("Invalid item")); continue; }
			FString Name = BatchToolHelper::GetItemString(Item, TEXT("name"));
			FString SavePath; Item->TryGetStringField(TEXT("save_path"), SavePath);
			if (Name.IsEmpty()) { Batch.AddFailure(i, TEXT("Missing name")); continue; }
			double InnerRadius = 400, FalloffDistance = 2000;
			Item->TryGetNumberField(TEXT("inner_radius"), InnerRadius); Item->TryGetNumberField(TEXT("falloff_distance"), FalloffDistance);
			FString ItemOut, ItemErr;
			HandleCreateSoundAttenuation(Name, SavePath, (float)InnerRadius, (float)FalloffDistance, ItemOut, ItemErr);
			if (ItemErr.IsEmpty())
			{
				TSharedPtr<FJsonObject> Extra = MakeShared<FJsonObject>();
				Extra->SetStringField(TEXT("name"), Name);
				Batch.AddSuccess(i, Extra);
			}
			else Batch.AddFailure(i, ItemErr);
		}
		Batch.Finalize(OutJsonString);
		return;
	}
	FString Name, SavePath;
	Args->TryGetStringField(TEXT("name"), Name);
	Args->TryGetStringField(TEXT("save_path"), SavePath);
	double InnerRadius = 400, FalloffDistance = 2000;
	Args->TryGetNumberField(TEXT("inner_radius"), InnerRadius); Args->TryGetNumberField(TEXT("falloff_distance"), FalloffDistance);
	HandleCreateSoundAttenuation(Name, SavePath, (float)InnerRadius, (float)FalloffDistance, OutJsonString, OutError);
}

void HandleCreateSoundConcurrencyFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	const TArray<TSharedPtr<FJsonValue>>* ItemsArray = nullptr;
	if (BatchToolHelper::TryGetBatchItems(Args, TEXT("concurrencies"), ItemsArray))
	{
		BatchToolHelper::FBatchResultBuilder Batch;
		for (int32 i = 0; i < ItemsArray->Num(); i++)
		{
			TSharedPtr<FJsonObject> Item = (*ItemsArray)[i]->AsObject();
			if (!Item.IsValid()) { Batch.AddFailure(i, TEXT("Invalid item")); continue; }
			FString Name = BatchToolHelper::GetItemString(Item, TEXT("name"), TEXT("asset_name"));
			FString SavePath; Item->TryGetStringField(TEXT("save_path"), SavePath);
			if (Name.IsEmpty()) { Batch.AddFailure(i, TEXT("Missing name")); continue; }
			FString ItemOut, ItemErr;
			HandleCreateSoundConcurrency(Name, SavePath, ItemOut, ItemErr);
			if (ItemErr.IsEmpty())
			{
				TSharedPtr<FJsonObject> Extra = MakeShared<FJsonObject>();
				Extra->SetStringField(TEXT("name"), Name);
				Batch.AddSuccess(i, Extra);
			}
			else Batch.AddFailure(i, ItemErr);
		}
		Batch.Finalize(OutJsonString);
		return;
	}
	FString Name, SavePath;
	Args->TryGetStringField(TEXT("name"), Name);
	if (Name.IsEmpty()) Args->TryGetStringField(TEXT("asset_name"), Name);
	Args->TryGetStringField(TEXT("save_path"), SavePath);
	HandleCreateSoundConcurrency(Name, SavePath, OutJsonString, OutError);
}

void HandleCreateAudioSubmixFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	const TArray<TSharedPtr<FJsonValue>>* ItemsArray = nullptr;
	if (BatchToolHelper::TryGetBatchItems(Args, TEXT("submixes"), ItemsArray))
	{
		BatchToolHelper::FBatchResultBuilder Batch;
		for (int32 i = 0; i < ItemsArray->Num(); i++)
		{
			TSharedPtr<FJsonObject> Item = (*ItemsArray)[i]->AsObject();
			if (!Item.IsValid()) { Batch.AddFailure(i, TEXT("Invalid item")); continue; }
			FString Name = BatchToolHelper::GetItemString(Item, TEXT("name"));
			FString SavePath; Item->TryGetStringField(TEXT("save_path"), SavePath);
			if (Name.IsEmpty()) { Batch.AddFailure(i, TEXT("Missing name")); continue; }
			double Volume = 1.0; Item->TryGetNumberField(TEXT("volume"), Volume);
			FString ItemOut, ItemErr;
			HandleCreateAudioSubmix(Name, SavePath, (float)Volume, ItemOut, ItemErr);
			if (ItemErr.IsEmpty())
			{
				TSharedPtr<FJsonObject> Extra = MakeShared<FJsonObject>();
				Extra->SetStringField(TEXT("name"), Name);
				Batch.AddSuccess(i, Extra);
			}
			else Batch.AddFailure(i, ItemErr);
		}
		Batch.Finalize(OutJsonString);
		return;
	}
	FString Name, SavePath;
	Args->TryGetStringField(TEXT("name"), Name);
	Args->TryGetStringField(TEXT("save_path"), SavePath);
	double Volume = 1.0; Args->TryGetNumberField(TEXT("volume"), Volume);
	HandleCreateAudioSubmix(Name, SavePath, (float)Volume, OutJsonString, OutError);
}

void HandleCreateSoundMixFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	const TArray<TSharedPtr<FJsonValue>>* ItemsArray = nullptr;
	if (BatchToolHelper::TryGetBatchItems(Args, TEXT("mixes"), ItemsArray))
	{
		BatchToolHelper::FBatchResultBuilder Batch;
		for (int32 i = 0; i < ItemsArray->Num(); i++)
		{
			TSharedPtr<FJsonObject> Item = (*ItemsArray)[i]->AsObject();
			if (!Item.IsValid()) { Batch.AddFailure(i, TEXT("Invalid item")); continue; }
			FString Name = BatchToolHelper::GetItemString(Item, TEXT("name"));
			FString SavePath; Item->TryGetStringField(TEXT("save_path"), SavePath);
			if (Name.IsEmpty()) { Batch.AddFailure(i, TEXT("Missing name")); continue; }
			double FadeIn = 0.0, FadeOut = 0.0, Duration = -1.0;
			Item->TryGetNumberField(TEXT("fade_in_time"), FadeIn);
			Item->TryGetNumberField(TEXT("fade_out_time"), FadeOut);
			Item->TryGetNumberField(TEXT("duration"), Duration);
			FString ItemOut, ItemErr;
			HandleCreateSoundMix(Name, SavePath, (float)FadeIn, (float)FadeOut, (float)Duration, ItemOut, ItemErr);
			if (ItemErr.IsEmpty())
			{
				TSharedPtr<FJsonObject> Extra = MakeShared<FJsonObject>();
				Extra->SetStringField(TEXT("name"), Name);
				Batch.AddSuccess(i, Extra);
			}
			else Batch.AddFailure(i, ItemErr);
		}
		Batch.Finalize(OutJsonString);
		return;
	}
	FString Name, SavePath;
	Args->TryGetStringField(TEXT("name"), Name);
	Args->TryGetStringField(TEXT("save_path"), SavePath);
	double FadeIn = 0.0, FadeOut = 0.0, Duration = -1.0;
	Args->TryGetNumberField(TEXT("fade_in_time"), FadeIn);
	Args->TryGetNumberField(TEXT("fade_out_time"), FadeOut);
	Args->TryGetNumberField(TEXT("duration"), Duration);
	HandleCreateSoundMix(Name, SavePath, (float)FadeIn, (float)FadeOut, (float)Duration, OutJsonString, OutError);
}

void HandleSetSoundWaveProperties(const FString& SoundWavePath,
	float PitchMultiplier, float VolumeMultiplier,
	const FString& Loop,
	const FString& LoadingBehavior,
	const FString& SoundClassPath,
	const FString& AttenuationPath,
	FString& OutJsonString, FString& OutError)
{
	if (SoundWavePath.IsEmpty()) { OutError = TEXT("sound_wave_path is required"); return; }
	UObject* AnyAsset = UEditorAssetLibrary::LoadAsset(SoundWavePath);
	USoundWave* SW = Cast<USoundWave>(AnyAsset);
	if (!SW)
	{
		if (AnyAsset && AnyAsset->IsA<USoundCue>())
		{
			const bool bWaveOnlyFieldsPassed = (PitchMultiplier >= 0.0f) || (VolumeMultiplier >= 0.0f)
				|| !Loop.IsEmpty() || !LoadingBehavior.IsEmpty();
			if (!bWaveOnlyFieldsPassed && (!SoundClassPath.IsEmpty() || !AttenuationPath.IsEmpty()))
			{
				FString CueOk, CueErr;
				if (!SoundClassPath.IsEmpty())
				{
					HandleSetSoundCueSoundClass(SoundWavePath, SoundClassPath, CueOk, CueErr);
					if (!CueErr.IsEmpty()) { OutError = CueErr; return; }
				}
				if (!AttenuationPath.IsEmpty())
				{
					FString AttOk, AttErr;
					HandleSetSoundCueAttenuation(SoundWavePath, AttenuationPath, AttOk, AttErr);
					if (!AttErr.IsEmpty()) { OutError = AttErr; return; }
				}
				TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
				Result->SetBoolField(TEXT("success"), true);
				Result->SetStringField(TEXT("auto_routed"), TEXT("set_sound_wave_properties → set_sound_cue_sound_class/attenuation (asset is a SoundCue, not a SoundWave)"));
				Result->SetStringField(TEXT("sound_cue_path"), SoundWavePath);
				if (!SoundClassPath.IsEmpty())  Result->SetStringField(TEXT("sound_class_path"),  SoundClassPath);
				if (!AttenuationPath.IsEmpty()) Result->SetStringField(TEXT("attenuation_path"), AttenuationPath);
				TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&OutJsonString);
				FJsonSerializer::Serialize(Result.ToSharedRef(), W);
				return;
			}
			OutError = FString::Printf(TEXT("'%s' is a SoundCue, not a SoundWave. Use set_sound_cue_attenuation / set_sound_cue_sound_class / set_sound_cue_output for cue-level changes, or build_sound_cue_from_spec to rebuild the graph."), *SoundWavePath);
		}
		else if (AnyAsset && AnyAsset->IsA<USoundClass>())
			OutError = FString::Printf(TEXT("'%s' is a SoundClass, not a SoundWave. Use set_sound_class_properties for volume/pitch/LPF on a class, or assign it to a cue via set_sound_cue_sound_class."), *SoundWavePath);
		else if (AnyAsset)
			OutError = FString::Printf(TEXT("'%s' is a %s, not a SoundWave. set_sound_wave_properties only works on imported SoundWave assets."), *SoundWavePath, *AnyAsset->GetClass()->GetName());
		else
			OutError = FString::Printf(TEXT("SoundWave not found: %s"), *SoundWavePath);
		return;
	}

	SW->Modify();

	if (PitchMultiplier >= 0.0f) SW->Pitch = PitchMultiplier;
	if (VolumeMultiplier >= 0.0f) SW->Volume = VolumeMultiplier;

	if (!Loop.IsEmpty())
		SW->bLooping = Loop.Equals(TEXT("true"), ESearchCase::IgnoreCase) || Loop.Equals(TEXT("1"), ESearchCase::IgnoreCase);

	if (!LoadingBehavior.IsEmpty())
	{
		uint8 Val = 0;
		if (LoadingBehavior.Equals(TEXT("RetainOnLoad"), ESearchCase::IgnoreCase))      Val = 1;
		else if (LoadingBehavior.Equals(TEXT("PrimeOnLoad"), ESearchCase::IgnoreCase))  Val = 2;
		else if (LoadingBehavior.Equals(TEXT("LoadOnDemand"), ESearchCase::IgnoreCase)) Val = 3;
		else if (LoadingBehavior.Equals(TEXT("ForceInline"), ESearchCase::IgnoreCase))  Val = 4;
		if (FEnumProperty* EP = FindFProperty<FEnumProperty>(USoundWave::StaticClass(), TEXT("LoadingBehavior")))
		{
			void* ValPtr = EP->ContainerPtrToValuePtr<void>(SW);
			EP->GetUnderlyingProperty()->SetIntPropertyValue(ValPtr, (int64)Val);
		}
		else if (FByteProperty* BP = FindFProperty<FByteProperty>(USoundWave::StaticClass(), TEXT("LoadingBehavior")))
			*BP->ContainerPtrToValuePtr<uint8>(SW) = Val;
	}

	if (!SoundClassPath.IsEmpty())
	{
		USoundClass* SC = Cast<USoundClass>(UEditorAssetLibrary::LoadAsset(SoundClassPath));
		if (!SC) { OutError = FString::Printf(TEXT("SoundClass not found: %s"), *SoundClassPath); return; }
		SW->SoundClassObject = SC;
	}

	if (!AttenuationPath.IsEmpty())
	{
		USoundAttenuation* SA = Cast<USoundAttenuation>(UEditorAssetLibrary::LoadAsset(AttenuationPath));
		if (!SA) { OutError = FString::Printf(TEXT("SoundAttenuation not found: %s"), *AttenuationPath); return; }
		SW->AttenuationSettings = SA;
	}

	SW->MarkPackageDirty();
	UEditorAssetLibrary::SaveAsset(SoundWavePath, false);

	OutJsonString = FString::Printf(
		TEXT("{\"success\":true,\"path\":\"%s\",\"pitch\":%.3f,\"volume\":%.3f,\"loop\":%s}"),
		*SoundWavePath, SW->Pitch, SW->Volume, SW->bLooping ? TEXT("true") : TEXT("false"));
	UE_LOG(LogMCPTool, Log, TEXT("set_sound_wave_properties: %s"), *SoundWavePath);
}

void HandleGetSoundWaveInfo(const FString& SoundWavePath, FString& OutJsonString, FString& OutError)
{
	if (SoundWavePath.IsEmpty()) { OutError = TEXT("sound_wave_path is required"); return; }
	UObject* AnyAsset = UEditorAssetLibrary::LoadAsset(SoundWavePath);
	USoundWave* SW = Cast<USoundWave>(AnyAsset);
	if (!SW)
	{
		if (AnyAsset)
		{
			OutError = FString::Printf(TEXT("'%s' is a %s, not a SoundWave. SoundWave assets are imported audio (.wav/.ogg/.flac), not Cues/Classes/Concurrencies. Use get_sound_cue_summary / get_asset_summary for the right asset type."), *SoundWavePath, *AnyAsset->GetClass()->GetName());
		}
		else
		{
			OutError = FString::Printf(TEXT("SoundWave does not exist at '%s'. Do not retry with the same path — the asset will not appear by polling. To find existing SoundWave assets use `find_asset_by_name(asset_type='SoundWave')`. To import a new one use `import_sound_wave(file_path='C:/path/to/wave.wav', destination_path='/Game/...')`. Build a SoundCue without a real wave by passing an empty waves[] array to build_sound_cue_from_spec — the cue will compile but produce silence in PIE."), *SoundWavePath);
		}
		return;
	}

	TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
	Obj->SetBoolField(TEXT("success"), true);
	Obj->SetStringField(TEXT("path"), SoundWavePath);
	Obj->SetNumberField(TEXT("duration_seconds"), SW->Duration);
	Obj->SetNumberField(TEXT("num_channels"), SW->NumChannels);
	Obj->SetNumberField(TEXT("sample_rate"), (double)SW->GetSampleRateForCurrentPlatform());
	Obj->SetBoolField(TEXT("loop"), SW->bLooping);
	Obj->SetNumberField(TEXT("pitch"), (double)SW->Pitch);
	Obj->SetNumberField(TEXT("volume"), (double)SW->Volume);

	FString LB = TEXT("Inherited");
	if (FEnumProperty* EP = FindFProperty<FEnumProperty>(USoundWave::StaticClass(), TEXT("LoadingBehavior")))
	{
		const void* ValPtr = EP->ContainerPtrToValuePtr<void>(SW);
		int64 Val = EP->GetUnderlyingProperty()->GetSignedIntPropertyValue(ValPtr);
		if      (Val == 1) LB = TEXT("RetainOnLoad");
		else if (Val == 2) LB = TEXT("PrimeOnLoad");
		else if (Val == 3) LB = TEXT("LoadOnDemand");
		else if (Val == 4) LB = TEXT("ForceInline");
	}
	Obj->SetStringField(TEXT("loading_behavior"), LB);

	if (SW->SoundClassObject)
		Obj->SetStringField(TEXT("sound_class"), SW->SoundClassObject->GetPathName());
	if (SW->AttenuationSettings)
		Obj->SetStringField(TEXT("attenuation"), SW->AttenuationSettings->GetPathName());

	FString OutStr;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutStr);
	FJsonSerializer::Serialize(Obj.ToSharedRef(), Writer);
	OutJsonString = OutStr;
}

static bool IsAudioModulationLoaded(FString& OutError)
{
	if (!FModuleManager::Get().IsModuleLoaded(TEXT("AudioModulation")))
	{ OutError = TEXT("AudioModulation plugin is not enabled in this project"); return false; }
	return true;
}

static UPackage* MakeAudioPackage(const FString& SavePath, const FString& Name, FString& OutAssetPath)
{
	FString CleanPath = SavePath.EndsWith(TEXT("/")) ? SavePath : (SavePath + TEXT("/"));
	OutAssetPath = CleanPath + Name;
	UPackage* Package = CreatePackage(*OutAssetPath);
	Package->FullyLoad();
	return Package;
}

void HandleCreateSoundControlBusFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError)
{
	if (!IsAudioModulationLoaded(OutError)) return;

	const TArray<TSharedPtr<FJsonValue>>* ItemsArray = nullptr;
	if (BatchToolHelper::TryGetBatchItems(Args, TEXT("buses"), ItemsArray))
	{
		BatchToolHelper::FBatchResultBuilder Batch;
		for (int32 i = 0; i < ItemsArray->Num(); i++)
		{
			TSharedPtr<FJsonObject> Item = (*ItemsArray)[i]->AsObject();
			if (!Item.IsValid()) { Batch.AddFailure(i, TEXT("Invalid item")); continue; }
			FString Name     = BatchToolHelper::GetItemString(Item, TEXT("name"), TEXT("bus_name"));
			FString SavePath = BatchToolHelper::GetItemString(Item, TEXT("save_path"), TEXT("path"));
			if (Name.IsEmpty()) { Batch.AddFailure(i, TEXT("Missing 'name'")); continue; }
			if (SavePath.IsEmpty()) SavePath = TEXT("/Game/Audio");

			FString AssetPath;
			UPackage* Package = MakeAudioPackage(SavePath, Name, AssetPath);
			USoundControlBus* Bus = NewObject<USoundControlBus>(Package, *Name, RF_Public | RF_Standalone);
			if (!Bus) { Batch.AddFailure(i, TEXT("Failed to create USoundControlBus")); continue; }
			Bus->MarkPackageDirty();
			FAssetRegistryModule::AssetCreated(Bus);

			TSharedPtr<FJsonObject> E = MakeShareable(new FJsonObject);
			E->SetStringField(TEXT("asset_path"), AssetPath);
			E->SetStringField(TEXT("name"), Name);
			Batch.AddSuccess(i, E);
		}
		Batch.Finalize(OutJson);
		return;
	}

	FString Name, SavePath;
	Args->TryGetStringField(TEXT("name"), Name);
	Args->TryGetStringField(TEXT("save_path"), SavePath);
	if (Name.IsEmpty()) { OutError = TEXT("Missing required parameter: name"); return; }
	if (SavePath.IsEmpty()) SavePath = TEXT("/Game/Audio");

	FString AssetPath;
	UPackage* Package = MakeAudioPackage(SavePath, Name, AssetPath);
	USoundControlBus* Bus = NewObject<USoundControlBus>(Package, *Name, RF_Public | RF_Standalone);
	if (!Bus) { OutError = TEXT("Failed to create USoundControlBus"); return; }
	Bus->MarkPackageDirty();
	FAssetRegistryModule::AssetCreated(Bus);
	OutJson = FString::Printf(TEXT("{\"success\":true,\"asset_path\":\"%s\"}"), *AssetPath);
}

void HandleCreateControlBusMixFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError)
{
	if (!IsAudioModulationLoaded(OutError)) return;

	const TArray<TSharedPtr<FJsonValue>>* ItemsArray = nullptr;
	if (BatchToolHelper::TryGetBatchItems(Args, TEXT("mixes"), ItemsArray))
	{
		BatchToolHelper::FBatchResultBuilder Batch;
		for (int32 i = 0; i < ItemsArray->Num(); i++)
		{
			TSharedPtr<FJsonObject> Item = (*ItemsArray)[i]->AsObject();
			if (!Item.IsValid()) { Batch.AddFailure(i, TEXT("Invalid item")); continue; }
			FString Name     = BatchToolHelper::GetItemString(Item, TEXT("name"), TEXT("mix_name"));
			FString SavePath = BatchToolHelper::GetItemString(Item, TEXT("save_path"), TEXT("path"));
			if (Name.IsEmpty()) { Batch.AddFailure(i, TEXT("Missing 'name'")); continue; }
			if (SavePath.IsEmpty()) SavePath = TEXT("/Game/Audio");

			FString AssetPath;
			UPackage* Package = MakeAudioPackage(SavePath, Name, AssetPath);
			USoundControlBusMix* Mix = NewObject<USoundControlBusMix>(Package, *Name, RF_Public | RF_Standalone);
			if (!Mix) { Batch.AddFailure(i, TEXT("Failed to create USoundControlBusMix")); continue; }
			Mix->MarkPackageDirty();
			FAssetRegistryModule::AssetCreated(Mix);

			TSharedPtr<FJsonObject> E = MakeShareable(new FJsonObject);
			E->SetStringField(TEXT("asset_path"), AssetPath);
			Batch.AddSuccess(i, E);
		}
		Batch.Finalize(OutJson);
		return;
	}

	FString Name, SavePath;
	Args->TryGetStringField(TEXT("name"), Name);
	Args->TryGetStringField(TEXT("save_path"), SavePath);
	if (Name.IsEmpty()) { OutError = TEXT("Missing required parameter: name"); return; }
	if (SavePath.IsEmpty()) SavePath = TEXT("/Game/Audio");

	FString AssetPath;
	UPackage* Package = MakeAudioPackage(SavePath, Name, AssetPath);
	USoundControlBusMix* Mix = NewObject<USoundControlBusMix>(Package, *Name, RF_Public | RF_Standalone);
	if (!Mix) { OutError = TEXT("Failed to create USoundControlBusMix"); return; }
	Mix->MarkPackageDirty();
	FAssetRegistryModule::AssetCreated(Mix);
	OutJson = FString::Printf(TEXT("{\"success\":true,\"asset_path\":\"%s\"}"), *AssetPath);
}

void HandleSetControlBusMixStageFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError)
{
	if (!IsAudioModulationLoaded(OutError)) return;

	FString MixPath; Args->TryGetStringField(TEXT("mix_path"), MixPath);
	if (MixPath.IsEmpty()) { OutError = TEXT("Missing required parameter: mix_path"); return; }

	USoundControlBusMix* Mix = Cast<USoundControlBusMix>(UEditorAssetLibrary::LoadAsset(MixPath));
	if (!Mix) { OutError = FString::Printf(TEXT("Could not load USoundControlBusMix at '%s'"), *MixPath); return; }

	const TArray<TSharedPtr<FJsonValue>>* ItemsArray = nullptr;
	if (BatchToolHelper::TryGetBatchItems(Args, TEXT("stages"), ItemsArray))
	{
		BatchToolHelper::FBatchResultBuilder Batch;
		for (int32 i = 0; i < ItemsArray->Num(); i++)
		{
			TSharedPtr<FJsonObject> Item = (*ItemsArray)[i]->AsObject();
			if (!Item.IsValid()) { Batch.AddFailure(i, TEXT("Invalid item")); continue; }
			FString BusPath = BatchToolHelper::GetItemString(Item, TEXT("bus_path"), TEXT("bus"));
			double TargetValue = 1.0, AttackTime = 0.1, ReleaseTime = 0.1;
			Item->TryGetNumberField(TEXT("target_value"), TargetValue);
			Item->TryGetNumberField(TEXT("attack_time"),  AttackTime);
			Item->TryGetNumberField(TEXT("release_time"), ReleaseTime);
			if (BusPath.IsEmpty()) { Batch.AddFailure(i, TEXT("Missing 'bus_path'")); continue; }

			USoundControlBus* Bus = Cast<USoundControlBus>(UEditorAssetLibrary::LoadAsset(BusPath));
			if (!Bus) { Batch.AddFailure(i, FString::Printf(TEXT("Could not load bus at '%s'"), *BusPath)); continue; }

			bool bFound = false;
			for (FSoundControlBusMixStage& Stage : Mix->MixStages)
			{
				if (Stage.Bus.Get() == Bus)
				{
					Stage.Value.TargetValue  = (float)TargetValue;
					Stage.Value.AttackTime   = (float)AttackTime;
					Stage.Value.ReleaseTime  = (float)ReleaseTime;
					bFound = true;
					break;
				}
			}
			if (!bFound)
			{
				FSoundControlBusMixStage NewStage;
				NewStage.Bus = Bus;
				NewStage.Value.TargetValue  = (float)TargetValue;
				NewStage.Value.AttackTime   = (float)AttackTime;
				NewStage.Value.ReleaseTime  = (float)ReleaseTime;
				Mix->MixStages.Add(NewStage);
			}

			TSharedPtr<FJsonObject> E = MakeShareable(new FJsonObject);
			E->SetStringField(TEXT("bus_path"), BusPath);
			E->SetNumberField(TEXT("target_value"), TargetValue);
			Batch.AddSuccess(i, E);
		}
		Mix->MarkPackageDirty();
		Batch.Finalize(OutJson);
		return;
	}

	FString BusPath; double TargetValue = 1.0, AttackTime = 0.1, ReleaseTime = 0.1;
	Args->TryGetStringField(TEXT("bus_path"), BusPath);
	Args->TryGetNumberField(TEXT("target_value"), TargetValue);
	Args->TryGetNumberField(TEXT("attack_time"),  AttackTime);
	Args->TryGetNumberField(TEXT("release_time"), ReleaseTime);
	if (BusPath.IsEmpty()) { OutError = TEXT("Missing required parameter: bus_path"); return; }

	USoundControlBus* Bus = Cast<USoundControlBus>(UEditorAssetLibrary::LoadAsset(BusPath));
	if (!Bus) { OutError = FString::Printf(TEXT("Could not load bus at '%s'"), *BusPath); return; }

	bool bFound = false;
	for (FSoundControlBusMixStage& Stage : Mix->MixStages)
	{
		if (Stage.Bus.Get() == Bus)
		{ Stage.Value.TargetValue=(float)TargetValue; Stage.Value.AttackTime=(float)AttackTime; Stage.Value.ReleaseTime=(float)ReleaseTime; bFound=true; break; }
	}
	if (!bFound)
	{
		FSoundControlBusMixStage NewStage;
		NewStage.Bus = Bus;
		NewStage.Value.TargetValue=(float)TargetValue; NewStage.Value.AttackTime=(float)AttackTime; NewStage.Value.ReleaseTime=(float)ReleaseTime;
		Mix->MixStages.Add(NewStage);
	}
	Mix->MarkPackageDirty();
	OutJson = FString::Printf(TEXT("{\"success\":true,\"mix_path\":\"%s\",\"bus_path\":\"%s\",\"target_value\":%f}"), *MixPath, *BusPath, TargetValue);
}

void HandleGetControlBusSummaryFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError)
{
	if (!IsAudioModulationLoaded(OutError)) return;

	const TArray<TSharedPtr<FJsonValue>>* ItemsArray = nullptr;
	if (BatchToolHelper::TryGetBatchItems(Args, TEXT("buses"), ItemsArray))
	{
		BatchToolHelper::FBatchResultBuilder Batch;
		for (int32 i = 0; i < ItemsArray->Num(); i++)
		{
			FString BusPath;
			if ((*ItemsArray)[i]->Type == EJson::String) BusPath = (*ItemsArray)[i]->AsString();
			else { TSharedPtr<FJsonObject> Item = (*ItemsArray)[i]->AsObject(); if (Item.IsValid()) Item->TryGetStringField(TEXT("bus_path"), BusPath); }
			if (BusPath.IsEmpty()) { Batch.AddFailure(i, TEXT("Empty bus_path")); continue; }

			USoundControlBus* Bus = Cast<USoundControlBus>(UEditorAssetLibrary::LoadAsset(BusPath));
			if (!Bus) { Batch.AddFailure(i, FString::Printf(TEXT("Could not load bus '%s'"), *BusPath)); continue; }

			TSharedPtr<FJsonObject> E = MakeShareable(new FJsonObject);
			E->SetStringField(TEXT("asset_path"), BusPath);
			E->SetStringField(TEXT("name"), Bus->GetName());
			Batch.AddSuccess(i, E);
		}
		Batch.Finalize(OutJson);
		return;
	}

	FString BusPath; Args->TryGetStringField(TEXT("bus_path"), BusPath);
	if (BusPath.IsEmpty()) { OutError = TEXT("Missing required parameter: bus_path"); return; }
	USoundControlBus* Bus = Cast<USoundControlBus>(UEditorAssetLibrary::LoadAsset(BusPath));
	if (!Bus) { OutError = FString::Printf(TEXT("Could not load bus at '%s'"), *BusPath); return; }
	OutJson = FString::Printf(TEXT("{\"success\":true,\"asset_path\":\"%s\",\"name\":\"%s\"}"),
		*BusPath, *Bus->GetName());
}

static UQuartzSubsystem* GetQuartzSubsystem(FString& OutError)
{
	UWorld* PlayWorld = GEditor ? GEditor->PlayWorld : nullptr;
	if (!PlayWorld) { OutError = TEXT("Quartz clock requires Play In Editor — start PIE first"); return nullptr; }
	UQuartzSubsystem* QSS = PlayWorld->GetSubsystem<UQuartzSubsystem>();
	if (!QSS) { OutError = TEXT("UQuartzSubsystem not available"); return nullptr; }
	return QSS;
}

void HandleCreateQuartzClock(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError)
{
	UWorld* PlayWorld = GEditor ? GEditor->PlayWorld : nullptr;
	UQuartzSubsystem* QSS = GetQuartzSubsystem(OutError);
	if (!QSS) return;

	FString ClockName; Args->TryGetStringField(TEXT("clock_name"), ClockName);
	if (ClockName.IsEmpty()) Args->TryGetStringField(TEXT("name"), ClockName);
	if (ClockName.IsEmpty()) ClockName = TEXT("DefaultQuartzClock");

	double BPM = 120.0; int32 NumBeats = 4;
	Args->TryGetNumberField(TEXT("bpm"), BPM);
	Args->TryGetNumberField(TEXT("num_beats"), NumBeats);

	FQuartzClockSettings ClockSettings;
	ClockSettings.TimeSignature.NumBeats = NumBeats;
	ClockSettings.TimeSignature.BeatType = EQuartzTimeSignatureQuantization::QuarterNote;

	UQuartzClockHandle* Handle = QSS->CreateNewClock(PlayWorld, FName(*ClockName), ClockSettings);
	if (!Handle) { OutError = FString::Printf(TEXT("Failed to create Quartz clock '%s'"), *ClockName); return; }

	FQuartzQuantizationBoundary Boundary;
	Handle->SetBeatsPerMinute(PlayWorld, Boundary, FOnQuartzCommandEventBP(), Handle, (float)BPM);

	OutJson = FString::Printf(TEXT("{\"success\":true,\"clock_name\":\"%s\",\"bpm\":%f,\"num_beats\":%d}"),
		*ClockName, BPM, NumBeats);
}

void HandleSetQuartzClockSettings(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError)
{
	UWorld* PlayWorld = GEditor ? GEditor->PlayWorld : nullptr;
	UQuartzSubsystem* QSS = GetQuartzSubsystem(OutError);
	if (!QSS) return;

	FString ClockName; Args->TryGetStringField(TEXT("clock_name"), ClockName);
	if (ClockName.IsEmpty()) { OutError = TEXT("Missing required parameter: clock_name"); return; }

	UQuartzClockHandle* Handle = QSS->GetHandleForClock(PlayWorld, FName(*ClockName));
	if (!Handle) { OutError = FString::Printf(TEXT("Quartz clock '%s' not found — create it first"), *ClockName); return; }

	double BPM = 0; int32 NumBeats = 0;
	Args->TryGetNumberField(TEXT("bpm"), BPM);
	Args->TryGetNumberField(TEXT("num_beats"), NumBeats);

	if (BPM > 0)
	{
		FQuartzQuantizationBoundary Boundary;
		Handle->SetBeatsPerMinute(PlayWorld, Boundary, FOnQuartzCommandEventBP(), Handle, (float)BPM);
	}

	OutJson = FString::Printf(TEXT("{\"success\":true,\"clock_name\":\"%s\",\"bpm_set\":%s}"),
		*ClockName, BPM > 0 ? TEXT("true") : TEXT("false"));
}

void HandleGetQuartzClockInfo(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError)
{
	UWorld* PlayWorld = GEditor ? GEditor->PlayWorld : nullptr;
	UQuartzSubsystem* QSS = GetQuartzSubsystem(OutError);
	if (!QSS) return;

	FString ClockName; Args->TryGetStringField(TEXT("clock_name"), ClockName);
	if (ClockName.IsEmpty()) { OutError = TEXT("Missing required parameter: clock_name"); return; }

	UQuartzClockHandle* Handle = QSS->GetHandleForClock(PlayWorld, FName(*ClockName));
	if (!Handle) { OutError = FString::Printf(TEXT("Quartz clock '%s' not found"), *ClockName); return; }

	bool bRunning = Handle->IsClockRunning(PlayWorld);
	float BPM = Handle->GetBeatsPerMinute(PlayWorld);

	OutJson = FString::Printf(TEXT("{\"success\":true,\"clock_name\":\"%s\",\"is_running\":%s,\"bpm\":%f}"),
		*ClockName, bRunning ? TEXT("true") : TEXT("false"), (double)BPM);
}

void HandleGenerateSound(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	FString Text;
	Args->TryGetStringField(TEXT("text"), Text);
	if (Text.IsEmpty()) { OutError = TEXT("'text' is required"); return; }

	FString Voice, AssetName, SavePath, Model;
	Args->TryGetStringField(TEXT("voice"),      Voice);
	Args->TryGetStringField(TEXT("asset_name"), AssetName);
	Args->TryGetStringField(TEXT("save_path"),  SavePath);
	Args->TryGetStringField(TEXT("model"),      Model);
	double SpeedD = 1.0;
	Args->TryGetNumberField(TEXT("speed"), SpeedD);

	FSoundGenRequest Req;
	Req.Text            = Text;
	Req.Voice           = Voice.IsEmpty()   ? FApiKeyManager::Get().GetActiveSoundGenVoice()   : Voice;
	Req.Model           = Model.IsEmpty()   ? FApiKeyManager::Get().GetActiveSoundGenModel()   : Model;
	Req.CustomAssetName = AssetName;
	Req.SavePath        = SavePath;
	Req.Speed           = static_cast<float>(SpeedD);

	FString ApiKey   = FApiKeyManager::Get().GetActiveSoundGenApiKey();
	FString Endpoint = FApiKeyManager::Get().GetActiveSoundGenEndpoint();

	FEvent* DoneEvent = FPlatformProcess::GetSynchEventFromPool(true);
	FSoundGenResult Result;

	IUECPCoreModule::Get().GetAssetGenService().GenerateSound(Req, ApiKey, Endpoint,
		[&Result, DoneEvent](const FSoundGenResult& R)
		{
			Result = R;
			DoneEvent->Trigger();
		});

	DoneEvent->Wait();
	FPlatformProcess::ReturnSynchEventToPool(DoneEvent);

	if (Result.bSuccess)
	{
		TSharedPtr<FJsonObject> J = MakeShareable(new FJsonObject);
		J->SetBoolField(TEXT("success"), true);
		J->SetStringField(TEXT("asset_path"), Result.AssetPath);
		J->SetStringField(TEXT("asset_name"), Result.AssetName);
		J->SetStringField(TEXT("file_path"),  Result.FilePath);

		FString Out;
		TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&Out);
		FJsonSerializer::Serialize(J.ToSharedRef(), W);
		OutJsonString = Out;
	}
	else
	{
		OutError = Result.ErrorMessage;
	}
}

void HandleAddSoundNodeFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString SoundCuePath, NodeType, SoundWavePath;
	Args->TryGetStringField(TEXT("sound_cue_path"), SoundCuePath);
	Args->TryGetStringField(TEXT("node_type"), NodeType);
	Args->TryGetStringField(TEXT("sound_wave_path"), SoundWavePath);
	double PosX = 0, PosY = 0;
	Args->TryGetNumberField(TEXT("position_x"), PosX);
	Args->TryGetNumberField(TEXT("position_y"), PosY);
	HandleAddSoundNode(SoundCuePath, NodeType, (int32)PosX, (int32)PosY, SoundWavePath, OutJsonString, OutError);
}

void HandleConnectSoundNodesFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString SoundCuePath, ChildNode, ParentNode;
	Args->TryGetStringField(TEXT("sound_cue_path"), SoundCuePath);
	Args->TryGetStringField(TEXT("child_node"), ChildNode);
	Args->TryGetStringField(TEXT("parent_node"), ParentNode);
	HandleConnectSoundNodes(SoundCuePath, ChildNode, ParentNode, OutJsonString, OutError);
}

void HandleSetSoundCueOutputFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString SoundCuePath, NodeName;
	Args->TryGetStringField(TEXT("sound_cue_path"), SoundCuePath);
	Args->TryGetStringField(TEXT("node_name"), NodeName);
	HandleSetSoundCueOutput(SoundCuePath, NodeName, OutJsonString, OutError);
}

void HandleSetSoundCueAttenuationFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString SoundCuePath, AttenuationPath;
	Args->TryGetStringField(TEXT("sound_cue_path"), SoundCuePath);
	Args->TryGetStringField(TEXT("attenuation_path"), AttenuationPath);
	HandleSetSoundCueAttenuation(SoundCuePath, AttenuationPath, OutJsonString, OutError);
}

void HandleSetSoundClassPropertiesFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString SoundClassPath;
	double Volume = -1, Pitch = -1, LPFFrequency = -1;
	Args->TryGetStringField(TEXT("sound_class_path"), SoundClassPath);
	Args->TryGetNumberField(TEXT("volume"), Volume);
	Args->TryGetNumberField(TEXT("pitch"), Pitch);
	Args->TryGetNumberField(TEXT("lpf_frequency"), LPFFrequency);
	HandleSetSoundClassProperties(SoundClassPath, (float)Volume, (float)Pitch, (float)LPFFrequency, OutJsonString, OutError);
}

void HandleSetSoundMixPropertiesFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString SoundMixPath, SoundClassPath;
	double VolumeAdj = -1.0, PitchAdj = -1.0;
	bool bApplyToChildren = false;
	if (!Args->TryGetStringField(TEXT("sound_mix_path"), SoundMixPath) || SoundMixPath.IsEmpty())
	{
		OutError = TEXT("Missing required parameter: sound_mix_path");
		return;
	}
	Args->TryGetStringField(TEXT("sound_class_path"), SoundClassPath);
	Args->TryGetNumberField(TEXT("volume_adjuster"), VolumeAdj);
	Args->TryGetNumberField(TEXT("pitch_adjuster"), PitchAdj);
	Args->TryGetBoolField(TEXT("apply_to_children"), bApplyToChildren);
	HandleSetSoundMixProperties(SoundMixPath, SoundClassPath, (float)VolumeAdj, (float)PitchAdj,
		bApplyToChildren, OutJsonString, OutError);
}

void HandleSetAudioSubmixPropertiesFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString SubmixPath, ParentSubmixPath;
	double Volume = -1.0;
	if (!Args->TryGetStringField(TEXT("submix_path"), SubmixPath) || SubmixPath.IsEmpty())
	{
		OutError = TEXT("Missing required parameter: submix_path");
		return;
	}
	Args->TryGetNumberField(TEXT("volume"), Volume);
	Args->TryGetStringField(TEXT("parent_submix_path"), ParentSubmixPath);
	HandleSetAudioSubmixProperties(SubmixPath, (float)Volume, ParentSubmixPath, OutJsonString, OutError);
}

void HandleSetSoundConcurrencyPropertiesFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString AssetPath, ResPolicy;
	double MaxCount = -1, StealFade = -1, VolScale = -1;
	bool bLimitToOwner = false;
	Args->TryGetStringField(TEXT("asset_path"), AssetPath);
	Args->TryGetStringField(TEXT("resolution_policy"), ResPolicy);
	Args->TryGetNumberField(TEXT("max_count"), MaxCount);
	Args->TryGetNumberField(TEXT("steal_fadeout_time"), StealFade);
	Args->TryGetNumberField(TEXT("volume_scale_attenuation"), VolScale);
	Args->TryGetBoolField(TEXT("limit_to_owner"), bLimitToOwner);
	HandleSetSoundConcurrencyProperties(AssetPath, (int32)MaxCount, ResPolicy,
		(float)StealFade, (float)VolScale, bLimitToOwner, OutJsonString, OutError);
}

void HandleAssignSoundConcurrencyFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString AssetPath, ConcurrencyPath;
	Args->TryGetStringField(TEXT("asset_path"), AssetPath);
	Args->TryGetStringField(TEXT("concurrency_path"), ConcurrencyPath);
	HandleAssignSoundConcurrency(AssetPath, ConcurrencyPath, OutJsonString, OutError);
}

void HandleGetSoundCueSummaryFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString SoundCuePath;
	Args->TryGetStringField(TEXT("sound_cue_path"), SoundCuePath);
	if (SoundCuePath.IsEmpty()) Args->TryGetStringField(TEXT("asset_path"), SoundCuePath);
	HandleGetSoundCueSummary(SoundCuePath, OutJsonString, OutError);
}

void HandleSetAttenuationShapeFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString AttenPath, Shape;
	double InnerRadius = 0, FalloffDistance = 0;
	Args->TryGetStringField(TEXT("attenuation_path"), AttenPath);
	Args->TryGetStringField(TEXT("shape"), Shape);
	Args->TryGetNumberField(TEXT("inner_radius"), InnerRadius);
	Args->TryGetNumberField(TEXT("falloff_distance"), FalloffDistance);
	HandleSetAttenuationShape(AttenPath, Shape, (float)InnerRadius, (float)FalloffDistance,
		OutJsonString, OutError);
}

void HandleSetAttenuationSpatializationFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString AttenPath, SpatMethod;
	bool bBinaural = false;
	Args->TryGetStringField(TEXT("attenuation_path"), AttenPath);
	Args->TryGetStringField(TEXT("spatialization_method"), SpatMethod);
	Args->TryGetBoolField(TEXT("enable_binaural"), bBinaural);
	HandleSetAttenuationSpatialization(AttenPath, SpatMethod, bBinaural, OutJsonString, OutError);
}

void HandleAddSubmixEffectFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString SubmixPath, EffectPath;
	Args->TryGetStringField(TEXT("submix_path"), SubmixPath);
	Args->TryGetStringField(TEXT("effect_preset_path"), EffectPath);
	HandleAddSubmixEffect(SubmixPath, EffectPath, OutJsonString, OutError);
}

void HandleRemoveSoundNodeFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString SoundCuePath, NodeName;
	Args->TryGetStringField(TEXT("sound_cue_path"), SoundCuePath);
	Args->TryGetStringField(TEXT("node_name"), NodeName);
	if (SoundCuePath.IsEmpty()) Args->TryGetStringField(TEXT("asset_path"), SoundCuePath);
	HandleRemoveSoundNode(SoundCuePath, NodeName, OutJsonString, OutError);
}

void HandleCreateDialogueVoiceFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString Name, SavePath, Gender, Plurality;
	Args->TryGetStringField(TEXT("name"), Name);
	Args->TryGetStringField(TEXT("save_path"), SavePath);
	Args->TryGetStringField(TEXT("gender"), Gender);
	Args->TryGetStringField(TEXT("plurality"), Plurality);
	HandleCreateDialogueVoice(Name, SavePath, Gender, Plurality, OutJsonString, OutError);
}

void HandleCreateDialogueWaveFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString Name, SavePath, SpokenText, SoundWavePath;
	Args->TryGetStringField(TEXT("name"), Name);
	Args->TryGetStringField(TEXT("save_path"), SavePath);
	Args->TryGetStringField(TEXT("spoken_text"), SpokenText);
	Args->TryGetStringField(TEXT("sound_wave_path"), SoundWavePath);
	HandleCreateDialogueWave(Name, SavePath, SpokenText, SoundWavePath, OutJsonString, OutError);
}

void HandleSetSoundWavePropertiesFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString Path, Loop, LoadingBehavior, SoundClassPath, AttenPath;
	double PitchMult = -1.0, VolMult = -1.0;
	Args->TryGetStringField(TEXT("sound_wave_path"), Path);
	Args->TryGetStringField(TEXT("loop"), Loop);
	Args->TryGetStringField(TEXT("loading_behavior"), LoadingBehavior);
	Args->TryGetStringField(TEXT("sound_class_path"), SoundClassPath);
	Args->TryGetStringField(TEXT("attenuation_path"), AttenPath);
	Args->TryGetNumberField(TEXT("pitch_multiplier"), PitchMult);
	Args->TryGetNumberField(TEXT("volume_multiplier"), VolMult);
	HandleSetSoundWaveProperties(Path, (float)PitchMult, (float)VolMult, Loop, LoadingBehavior,
		SoundClassPath, AttenPath, OutJsonString, OutError);
}

void HandleGetSoundWaveInfoFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString Path;
	Args->TryGetStringField(TEXT("sound_wave_path"), Path);
	if (Path.IsEmpty()) Args->TryGetStringField(TEXT("asset_path"), Path);
	HandleGetSoundWaveInfo(Path, OutJsonString, OutError);
}

void HandleBuildSoundCueFromSpecFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }

	FString AssetPath;
	Args->TryGetStringField(TEXT("asset_path"), AssetPath);
	if (AssetPath.IsEmpty()) { OutError = TEXT("Missing required parameter: asset_path"); return; }

	FString PackagePath, CueName;
	if (!AssetPath.Split(TEXT("/"), &PackagePath, &CueName, ESearchCase::IgnoreCase, ESearchDir::FromEnd))
	{
		OutError = FString::Printf(TEXT("Invalid asset_path (expected /Game/Folder/AssetName): %s"), *AssetPath);
		return;
	}

	{
		FString OutputId;
		Args->TryGetStringField(TEXT("output"), OutputId);
		const TArray<TSharedPtr<FJsonValue>>* PreNodesArray = nullptr;
		const TArray<TSharedPtr<FJsonValue>>* PreConnsArray = nullptr;
		const TArray<TSharedPtr<FJsonValue>>* PreWavesArray = nullptr;
		Args->TryGetArrayField(TEXT("nodes"),       PreNodesArray);
		Args->TryGetArrayField(TEXT("connections"), PreConnsArray);
		Args->TryGetArrayField(TEXT("waves"),       PreWavesArray);
		if (!OutputId.IsEmpty() && PreNodesArray)
		{
			FString OutputType;
			for (const TSharedPtr<FJsonValue>& NV : *PreNodesArray)
			{
				TSharedPtr<FJsonObject> NSpec = NV->AsObject();
				if (!NSpec.IsValid()) continue;
				FString NId, NType;
				NSpec->TryGetStringField(TEXT("id"),   NId);
				NSpec->TryGetStringField(TEXT("type"), NType);
				if (NId == OutputId) { OutputType = NType; break; }
			}
			const bool bIsContainer = OutputType.Equals(TEXT("Random"),       ESearchCase::IgnoreCase)
				|| OutputType.Equals(TEXT("Mixer"),        ESearchCase::IgnoreCase)
				|| OutputType.Equals(TEXT("Concatenator"), ESearchCase::IgnoreCase);
			if (bIsContainer)
			{
				int32 IncomingCount = 0;
				if (PreConnsArray)
				{
					for (const TSharedPtr<FJsonValue>& CV : *PreConnsArray)
					{
						TSharedPtr<FJsonObject> CSpec = CV->AsObject();
						if (!CSpec.IsValid()) continue;
						FString ToId;
						CSpec->TryGetStringField(TEXT("to"), ToId);
						if (ToId == OutputId) ++IncomingCount;
					}
				}
				const bool bWavesProvided = PreWavesArray && PreWavesArray->Num() > 0;
				if (IncomingCount == 0 && !bWavesProvided)
				{
					OutError = FString::Printf(
						TEXT("Pre-flight: output '%s' is a %s container but has no children — the audio engine crashes when walking an empty Random/Mixer/Concatenator. Either: (1) add `waves:[\"/Game/Path/SW1\",\"/Game/Path/SW2\"]` and connections=[{from:\"wave_0\",to:\"%s\"},{from:\"wave_1\",to:\"%s\"}] to wire wave players to the container; OR (2) drop the container and set `output` directly to a single wave node id. Use find_asset_by_name(asset_type='SoundWave') to discover available SoundWave assets."),
						*OutputId, *OutputType, *OutputId, *OutputId);
					return;
				}
			}
		}
	}

	if (AssetCreationHelper::BailIfDifferentClassExists(CueName, PackagePath, TEXT("SoundCue"), OutJsonString, OutError))
		return;

	IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
	USoundCueFactoryNew* Factory = NewObject<USoundCueFactoryNew>();
	UObject* Asset = nullptr;
	if (!AssetCreationHelper::DoesAssetExist(CueName, PackagePath))
	{
		Asset = AssetTools.CreateAsset(CueName, PackagePath, USoundCue::StaticClass(), Factory);
	}
	USoundCue* SoundCue = Cast<USoundCue>(Asset);
	if (!SoundCue)
	{
		SoundCue = Cast<USoundCue>(UEditorAssetLibrary::LoadAsset(AssetPath));
		if (!SoundCue)
		{
			OutError = FString::Printf(TEXT("Failed to create or load SoundCue at: %s"), *AssetPath);
			return;
		}
	}

	TMap<FString, USoundNode*> NodeMap;
	int32 NodesCreated = 0;
	int32 ConnectionsWired = 0;

	const TArray<TSharedPtr<FJsonValue>>* WavesArray = nullptr;
	if (Args->TryGetArrayField(TEXT("waves"), WavesArray) && WavesArray)
	{
		for (int32 i = 0; i < WavesArray->Num(); i++)
		{
			FString WavePath = (*WavesArray)[i]->AsString();
			if (WavePath.IsEmpty()) continue;
			USoundNode* WaveNode = CreateNodeOfType(SoundCue, TEXT("Wave"), WavePath);
			if (!WaveNode) continue;
			FString NodeId = FString::Printf(TEXT("wave_%d"), i);
			SetupGraphNode(SoundCue, WaveNode, -700, i * 200);
			NodeMap.Add(NodeId, WaveNode);
			NodesCreated++;
		}
	}

	const TArray<TSharedPtr<FJsonValue>>* NodesArray = nullptr;
	if (Args->TryGetArrayField(TEXT("nodes"), NodesArray) && NodesArray)
	{
		int32 NodeX = -300;
		int32 NodeY = 0;
		for (const TSharedPtr<FJsonValue>& NodeVal : *NodesArray)
		{
			TSharedPtr<FJsonObject> NodeSpec = NodeVal->AsObject();
			if (!NodeSpec.IsValid()) continue;
			FString NodeId, NodeType;
			NodeSpec->TryGetStringField(TEXT("id"), NodeId);
			NodeSpec->TryGetStringField(TEXT("type"), NodeType);
			if (NodeId.IsEmpty() || NodeType.IsEmpty()) continue;

			USoundNode* NewNode = CreateNodeOfType(SoundCue, NodeType, TEXT(""));
			if (!NewNode) continue;
			SetupGraphNode(SoundCue, NewNode, NodeX, NodeY);
			NodeX -= 250;
			NodeMap.Add(NodeId, NewNode);
			NodesCreated++;

			const TSharedPtr<FJsonObject>* PropsPtr = nullptr;
			if (NodeSpec->TryGetObjectField(TEXT("props"), PropsPtr) && PropsPtr && (*PropsPtr).IsValid())
			{
				for (const auto& KV : (*PropsPtr)->Values)
				{
					FProperty* Prop = NewNode->GetClass()->FindPropertyByName(FName(*KV.Key));
					if (!Prop) continue;
					void* ValuePtr = Prop->ContainerPtrToValuePtr<void>(NewNode);
					FString StrVal;
					if (KV.Value->Type == EJson::String)
						StrVal = KV.Value->AsString();
					else if (KV.Value->Type == EJson::Number)
						StrVal = FString::SanitizeFloat(KV.Value->AsNumber());
					else if (KV.Value->Type == EJson::Boolean)
						StrVal = KV.Value->AsBool() ? TEXT("True") : TEXT("False");
					if (!StrVal.IsEmpty())
						Prop->ImportText_Direct(*StrVal, ValuePtr, NewNode, PPF_None);
				}
			}
		}
	}

	const TArray<TSharedPtr<FJsonValue>>* ConnsArray = nullptr;
	if (Args->TryGetArrayField(TEXT("connections"), ConnsArray) && ConnsArray)
	{
		for (const TSharedPtr<FJsonValue>& ConnVal : *ConnsArray)
		{
			TSharedPtr<FJsonObject> Conn = ConnVal->AsObject();
			if (!Conn.IsValid()) continue;
			FString FromId, ToId;
			Conn->TryGetStringField(TEXT("from"), FromId);
			Conn->TryGetStringField(TEXT("to"), ToId);
			USoundNode* FromNode = NodeMap.FindRef(FromId);
			USoundNode* ToNode   = NodeMap.FindRef(ToId);
			if (!FromNode || !ToNode || ToNode->ChildNodes.Contains(FromNode)) continue;

			ToNode->ChildNodes.Add(FromNode);
			if (USoundCueGraphNode* GN = Cast<USoundCueGraphNode>(ToNode->GraphNode))
				GN->ReconstructNode();

			if      (USoundNodeMixer*       M = Cast<USoundNodeMixer>(ToNode))       M->InputVolume.Add(1.0f);
			else if (USoundNodeRandom*      R = Cast<USoundNodeRandom>(ToNode))      R->Weights.Add(1.0f);
			else if (USoundNodeConcatenator*C = Cast<USoundNodeConcatenator>(ToNode))C->InputVolume.Add(1.0f);
			ConnectionsWired++;
		}
	}

	FString OutputId;
	Args->TryGetStringField(TEXT("output"), OutputId);
	if (!OutputId.IsEmpty())
	{
		USoundNode* OutNode = NodeMap.FindRef(OutputId);
		if (OutNode) SoundCue->FirstNode = OutNode;
	}

	FString AttenuationPath;
	Args->TryGetStringField(TEXT("attenuation"), AttenuationPath);
	if (!AttenuationPath.IsEmpty())
	{
		USoundAttenuation* Att = Cast<USoundAttenuation>(UEditorAssetLibrary::LoadAsset(AttenuationPath));
		if (Att) SoundCue->AttenuationSettings = Att;
	}

	TArray<FString> EmptyContainers = CollectEmptyContainers(SoundCue);
	if (EmptyContainers.Num() > 0)
	{
		SoundCue->FirstNode = nullptr;
		OutError = FString::Printf(
			TEXT("SoundCue graph reachable from output has %d container node(s) with 0 children: %s. The audio engine crashes when it walks an empty Random/Mixer/Concatenator. Wire children before connecting these to output, or omit them from connections[]. The cue's FirstNode was cleared to make the asset safe to open."),
			EmptyContainers.Num(), *FString::Join(EmptyContainers, TEXT(", ")));
		SoundCue->MarkPackageDirty();
		UEditorAssetLibrary::SaveAsset(AssetPath, false);
		return;
	}

#if WITH_EDITOR
	SoundCue->LinkGraphNodesFromSoundNodes();
	SoundCue->CacheAggregateValues();
#endif

	SoundCue->MarkPackageDirty();
	UEditorAssetLibrary::SaveAsset(AssetPath, false);

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("asset_path"), AssetPath);
	Result->SetNumberField(TEXT("nodes_created"), NodesCreated);
	Result->SetNumberField(TEXT("connections_wired"), ConnectionsWired);
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutJsonString);
	FJsonSerializer::Serialize(Result.ToSharedRef(), Writer);
}

void HandleSetSoundNodePropertyFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }

	FString CuePath, NodeName, PropertyName;
	Args->TryGetStringField(TEXT("sound_cue_path"), CuePath);
	Args->TryGetStringField(TEXT("node_name"),      NodeName);
	Args->TryGetStringField(TEXT("property_name"),  PropertyName);
	if (NodeName.IsEmpty() || PropertyName.IsEmpty())
	{ OutError = TEXT("node_name and property_name are required"); return; }

	USoundCue* Cue = LoadObject<USoundCue>(nullptr, *CuePath);
	if (!Cue) { OutError = FString::Printf(TEXT("Could not load SoundCue: %s"), *CuePath); return; }

	USoundNode* Node = FindNodeByName(Cue, NodeName);
	if (!Node) { OutError = FString::Printf(TEXT("Node '%s' not found in SoundCue."), *NodeName); return; }

	FProperty* Prop = Node->GetClass()->FindPropertyByName(FName(*PropertyName));
	if (!Prop) { OutError = FString::Printf(TEXT("Property '%s' not found on %s."), *PropertyName, *Node->GetClass()->GetName()); return; }

	const TSharedPtr<FJsonValue>* RawVal = nullptr;
	FString StrVal;
	if (Args->Values.Find(TEXT("property_value")))
	{
		const TSharedPtr<FJsonValue>& V = Args->Values[TEXT("property_value")];
		if (V.IsValid())
		{
			if (V->Type == EJson::String)        StrVal = V->AsString();
			else if (V->Type == EJson::Number)   StrVal = FString::SanitizeFloat(V->AsNumber());
			else if (V->Type == EJson::Boolean)  StrVal = V->AsBool() ? TEXT("True") : TEXT("False");
		}
	}
	if (StrVal.IsEmpty()) { OutError = TEXT("property_value is required (string, number, or bool)."); return; }

	Node->Modify();

	double ArrayIdx = -1;
	const bool bHaveIndex = Args->TryGetNumberField(TEXT("array_index"), ArrayIdx);
	if (bHaveIndex && CastField<FArrayProperty>(Prop))
	{
		FArrayProperty* AP = CastField<FArrayProperty>(Prop);
		FScriptArrayHelper Helper(AP, AP->ContainerPtrToValuePtr<void>(Node));
		const int32 Idx = (int32)ArrayIdx;
		while (Helper.Num() <= Idx) Helper.AddValue();
		void* ElemPtr = Helper.GetRawPtr(Idx);
		AP->Inner->ImportText_Direct(*StrVal, ElemPtr, Node, PPF_None);
	}
	else
	{
		void* ValuePtr = Prop->ContainerPtrToValuePtr<void>(Node);
		Prop->ImportText_Direct(*StrVal, ValuePtr, Node, PPF_None);
	}

	if (USoundCueGraphNode* GN = Cast<USoundCueGraphNode>(Node->GraphNode))
	{
		GN->ReconstructNode();
	}
	Cue->MarkPackageDirty();
	UEditorAssetLibrary::SaveAsset(CuePath, false);

	OutJsonString = FString::Printf(
		TEXT("{\"success\":true,\"node_name\":\"%s\",\"property\":\"%s\",\"value\":\"%s\"}"),
		*NodeName, *PropertyName, *StrVal);
}

}
