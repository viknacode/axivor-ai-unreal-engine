// Copyright 2026, BlueprintsLab, All rights reserved

#include "Tools/AnalysisTools.h"
#include "Managers/SettingsManager.h"
#include "Editor.h"
#include "Engine/LevelScriptBlueprint.h"
#include "EditorAssetLibrary.h"
#include "Describers/BpGraphDescriber.h"
#include "Describers/MaterialGraphDescriber.h"
#include "Describers/MaterialNodeDescriber.h"
#include "Describers/BtGraphDescriber.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "BlueprintEditor.h"
#include "WidgetBlueprintEditor.h"
#include "BehaviorTreeEditor.h"
#include <Editor/MaterialEditor/Private/MaterialEditor.h>
#include "Serialization/JsonSerializer.h"
#include "Dom/JsonObject.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstance.h"
#include "BehaviorTree/BehaviorTree.h"
#include "Engine/DataTable.h"
#include "Engine/UserDefinedEnum.h"
#include "Misc/EngineVersionComparison.h"
#if UE_VERSION_OLDER_THAN(5, 5, 0)
#include "Engine/UserDefinedStruct.h"
#else
#include "StructUtils/UserDefinedStruct.h"
#endif
#include "LevelSequence.h"
#include "MovieScene.h"
#include "Engine/StaticMesh.h"
#include "Engine/SkeletalMesh.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "Engine/Texture2D.h"
#include "Sound/SoundWave.h"
#include "Sound/SoundCue.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimMontage.h"
#include "Animation/BlendSpace.h"
#include "Curves/CurveFloat.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "Particles/ParticleSystem.h"

namespace AnalysisTools
{

void HandleGetMaterialGraphSummary(const FString& MaterialPath, FString& OutSummary, FString& OutError)
{
	UObject* LoadedAsset = StaticLoadObject(UObject::StaticClass(), nullptr, *MaterialPath);
	if (!LoadedAsset)
	{
		OutError = FString::Printf(TEXT("Failed to load any asset at path: %s."), *MaterialPath);
		return;
	}

	if (LoadedAsset->IsA(UMaterialInstance::StaticClass()))
	{
		OutError = TEXT("The selected asset is a Material Instance, which does not have a node graph.");
		return;
	}

	UMaterial* TargetMaterial = Cast<UMaterial>(LoadedAsset);
	if (!TargetMaterial)
	{
		OutError = FString::Printf(TEXT("The asset at path '%s' is not a base Material."), *MaterialPath);
		return;
	}

	FMaterialGraphDescriber Describer;
	OutSummary = Describer.Describe(TargetMaterial);

	if (OutSummary.IsEmpty())
	{
		OutError = TEXT("The summarizer returned an empty string or an error occurred.");
	}
}

void HandleGetAssetSummary(const FString& AssetPath, FString& OutSummary, FString& OutError)
{
	UObject* LoadedAsset = UEditorAssetLibrary::LoadAsset(AssetPath);
	if (!LoadedAsset)
	{
		if (UEditorAssetLibrary::DoesDirectoryExist(AssetPath))
		{
			TArray<FString> Assets = UEditorAssetLibrary::ListAssets(AssetPath,  false,  false);
			if (Assets.Num() == 0)
			{
				OutSummary = FString::Printf(TEXT("Folder: %s\nContains 0 assets (recursive=false). The path is a directory, not an asset. Use find_asset_by_name(name_pattern='*', search_path='%s') to list recursively, or supply the full asset path /Game/Folder/MyAsset.MyAsset."), *AssetPath, *AssetPath);
				return;
			}
			FString List;
			const int32 Cap = FMath::Min(Assets.Num(), 50);
			for (int32 i = 0; i < Cap; ++i) List += TEXT("\n  ") + Assets[i];
			OutSummary = FString::Printf(TEXT("Folder: %s\nContains %d asset(s)%s%s"),
				*AssetPath, Assets.Num(), *List,
				Assets.Num() > Cap ? *FString::Printf(TEXT("\n  ... and %d more (use find_asset_by_name to filter)"), Assets.Num() - Cap) : TEXT(""));
			return;
		}
		OutError = FString::Printf(TEXT("No asset at path: %s. Check spelling and confirm the path includes the asset name (e.g. /Game/Folder/MyAsset, NOT /Game/Folder). For folders use find_asset_by_name(search_path='%s'). For unknown locations use find_asset_by_name(name_pattern='*PartialName*')."), *AssetPath, *AssetPath);
		return;
	}

	if (UBlueprint* Blueprint = Cast<UBlueprint>(LoadedAsset))
	{
		OutError = FString::Printf(
			TEXT("Asset at '%s' is a Blueprint. Use get_blueprint_skeleton(blueprint_path='%s') for the cheap structural overview, then get_blueprint_graph(detail='outline') / get_blueprint_subgraph for targeted reads. get_asset_summary is for non-Blueprint assets only."),
			*AssetPath, *AssetPath);
		return;
	}
	else if (UMaterial* Material = Cast<UMaterial>(LoadedAsset))
	{
		FMaterialGraphDescriber Describer;
		OutSummary = Describer.Describe(Material);
	}
	else if (UBehaviorTree* BehaviorTree = Cast<UBehaviorTree>(LoadedAsset))
	{
		FBtGraphDescriber Describer;
		OutSummary = Describer.Describe(BehaviorTree);
	}
	else if (UDataTable* DataTable = Cast<UDataTable>(LoadedAsset))
	{
		FString RowStructName = DataTable->GetRowStruct() ? DataTable->GetRowStruct()->GetName() : TEXT("Unknown");
		TArray<FName> RowNames = DataTable->GetRowNames();
		OutSummary = FString::Printf(TEXT("DataTable: %s\nRow Struct: %s\nRow Count: %d\n"), *DataTable->GetName(), *RowStructName, RowNames.Num());
		if (const UScriptStruct* RowStruct = DataTable->GetRowStruct())
		{
			OutSummary += TEXT("Columns:\n");
			for (TFieldIterator<FProperty> It(RowStruct); It; ++It)
				OutSummary += FString::Printf(TEXT("  %s: %s\n"), *It->GetName(), *It->GetCPPType());
		}
		if (RowNames.Num() > 0)
		{
			OutSummary += TEXT("Rows: ");
			for (int32 i = 0; i < FMath::Min(RowNames.Num(), 10); i++)
				OutSummary += RowNames[i].ToString() + TEXT(", ");
			if (RowNames.Num() > 10)
				OutSummary += FString::Printf(TEXT("... (%d more)"), RowNames.Num() - 10);
		}
	}
	else if (UUserDefinedStruct* UDS = Cast<UUserDefinedStruct>(LoadedAsset))
	{
		OutSummary = FString::Printf(TEXT("UserDefinedStruct: %s\nMembers:\n"), *UDS->GetName());
		for (TFieldIterator<FProperty> It(UDS); It; ++It)
			OutSummary += FString::Printf(TEXT("  %s: %s\n"), *It->GetDisplayNameText().ToString(), *It->GetCPPType());
	}
	else if (UUserDefinedEnum* UDE = Cast<UUserDefinedEnum>(LoadedAsset))
	{
		OutSummary = FString::Printf(TEXT("UserDefinedEnum: %s\nValues:\n"), *UDE->GetName());
		for (int32 i = 0; i < UDE->NumEnums() - 1; i++)
			OutSummary += FString::Printf(TEXT("  %s = %lld\n"), *UDE->GetNameStringByIndex(i), UDE->GetValueByIndex(i));
	}
	else if (UMaterialInstance* MatInst = Cast<UMaterialInstance>(LoadedAsset))
	{
		OutSummary = FString::Printf(TEXT("MaterialInstance: %s\n"), *MatInst->GetName());
		if (MatInst->Parent)
			OutSummary += FString::Printf(TEXT("Parent: %s\n"), *MatInst->Parent->GetName());
		TArray<FMaterialParameterInfo> ScalarParams, VectorParams, TextureParams;
		TArray<FGuid> ScalarGuids, VectorGuids, TextureGuids;
		MatInst->GetAllScalarParameterInfo(ScalarParams, ScalarGuids);
		MatInst->GetAllVectorParameterInfo(VectorParams, VectorGuids);
		MatInst->GetAllTextureParameterInfo(TextureParams, TextureGuids);
		if (ScalarParams.Num() > 0)
		{
			OutSummary += TEXT("Scalar Parameters:\n");
			for (auto& P : ScalarParams) { float Val = 0.f; MatInst->GetScalarParameterValue(P, Val); OutSummary += FString::Printf(TEXT("  %s = %.3f\n"), *P.Name.ToString(), Val); }
		}
		if (VectorParams.Num() > 0)
		{
			OutSummary += TEXT("Vector Parameters:\n");
			for (auto& P : VectorParams) { FLinearColor Val; MatInst->GetVectorParameterValue(P, Val); OutSummary += FString::Printf(TEXT("  %s = (R=%.2f,G=%.2f,B=%.2f,A=%.2f)\n"), *P.Name.ToString(), Val.R, Val.G, Val.B, Val.A); }
		}
		if (TextureParams.Num() > 0)
		{
			OutSummary += TEXT("Texture Parameters:\n");
			for (auto& P : TextureParams) { UTexture* Tex = nullptr; MatInst->GetTextureParameterValue(P, Tex); OutSummary += FString::Printf(TEXT("  %s = %s\n"), *P.Name.ToString(), Tex ? *Tex->GetName() : TEXT("None")); }
		}
	}
	else if (ULevelSequence* Sequence = Cast<ULevelSequence>(LoadedAsset))
	{
		OutSummary = FString::Printf(TEXT("LevelSequence: %s\n"), *Sequence->GetName());
		if (UMovieScene* MovieScene = Sequence->GetMovieScene())
		{
			OutSummary += FString::Printf(TEXT("Tracks: %d\n"), MovieScene->GetTracks().Num());
			for (UMovieSceneTrack* Track : MovieScene->GetTracks())
			{
				if (Track)
					OutSummary += FString::Printf(TEXT("  Track: %s (%s)\n"), *Track->GetDisplayName().ToString(), *Track->GetClass()->GetName());
			}
			FFrameRate TickRes = MovieScene->GetTickResolution();
			FFrameRate DisplayRate = MovieScene->GetDisplayRate();
			TRange<FFrameNumber> PlayRange = MovieScene->GetPlaybackRange();
			double StartSec = TickRes.AsSeconds(PlayRange.GetLowerBoundValue());
			double EndSec = TickRes.AsSeconds(PlayRange.GetUpperBoundValue());
			OutSummary += FString::Printf(TEXT("Duration: %.2fs (%.1f - %.1f)\nDisplay Rate: %d fps\n"), EndSec - StartSec, StartSec, EndSec, DisplayRate.Numerator);
			OutSummary += FString::Printf(TEXT("Bindings: %d\n"), MovieScene->GetPossessableCount() + MovieScene->GetSpawnableCount());
		}
	}
	else if (UStaticMesh* SMesh = Cast<UStaticMesh>(LoadedAsset))
	{
		OutSummary = FString::Printf(TEXT("StaticMesh: %s\n"), *SMesh->GetName());
		OutSummary += FString::Printf(TEXT("LOD Count: %d\n"), SMesh->GetNumLODs());
		if (SMesh->GetRenderData() && SMesh->GetRenderData()->LODResources.Num() > 0)
		{
			const FStaticMeshLODResources& LOD0 = SMesh->GetRenderData()->LODResources[0];
			OutSummary += FString::Printf(TEXT("Triangles (LOD0): %d\nVertices (LOD0): %d\n"), LOD0.GetNumTriangles(), LOD0.GetNumVertices());
		}
		OutSummary += FString::Printf(TEXT("Material Slots: %d\n"), SMesh->GetStaticMaterials().Num());
		for (int32 i = 0; i < SMesh->GetStaticMaterials().Num(); i++)
		{
			const FStaticMaterial& Mat = SMesh->GetStaticMaterials()[i];
			OutSummary += FString::Printf(TEXT("  [%d] %s\n"), i, Mat.MaterialInterface ? *Mat.MaterialInterface->GetName() : TEXT("None"));
		}
		FBoxSphereBounds Bounds = SMesh->GetBounds();
		OutSummary += FString::Printf(TEXT("Bounds: (%.1f, %.1f, %.1f)\n"), Bounds.BoxExtent.X * 2, Bounds.BoxExtent.Y * 2, Bounds.BoxExtent.Z * 2);
	}
	else if (USkeletalMesh* SKMesh = Cast<USkeletalMesh>(LoadedAsset))
	{
		OutSummary = FString::Printf(TEXT("SkeletalMesh: %s\n"), *SKMesh->GetName());
		if (const FSkeletalMeshRenderData* RenderData = SKMesh->GetResourceForRendering())
		{
			OutSummary += FString::Printf(TEXT("LOD Count: %d\n"), RenderData->LODRenderData.Num());
			if (RenderData->LODRenderData.Num() > 0)
				OutSummary += FString::Printf(TEXT("Vertices (LOD0): %d\n"), RenderData->LODRenderData[0].GetNumVertices());
		}
		if (SKMesh->GetSkeleton())
		{
			OutSummary += FString::Printf(TEXT("Skeleton: %s\n"), *SKMesh->GetSkeleton()->GetName());
			OutSummary += FString::Printf(TEXT("Bone Count: %d\n"), SKMesh->GetRefSkeleton().GetNum());
		}
		OutSummary += FString::Printf(TEXT("Material Slots: %d\n"), SKMesh->GetMaterials().Num());
		if (SKMesh->GetMorphTargets().Num() > 0)
			OutSummary += FString::Printf(TEXT("Morph Targets: %d\n"), SKMesh->GetMorphTargets().Num());
	}
	else if (UTexture2D* Texture = Cast<UTexture2D>(LoadedAsset))
	{
		OutSummary = FString::Printf(TEXT("Texture2D: %s\n"), *Texture->GetName());
		OutSummary += FString::Printf(TEXT("Resolution: %dx%d\n"), Texture->GetSizeX(), Texture->GetSizeY());
		OutSummary += FString::Printf(TEXT("Format: %s\n"), GetPixelFormatString(Texture->GetPixelFormat()));
		OutSummary += FString::Printf(TEXT("SRGB: %s\n"), Texture->SRGB ? TEXT("Yes") : TEXT("No"));
		OutSummary += FString::Printf(TEXT("Compression: %s\n"), *UEnum::GetValueAsString(Texture->CompressionSettings));
		OutSummary += FString::Printf(TEXT("Mip Count: %d\n"), Texture->GetNumMips());
	}
	else if (USoundWave* SoundWave = Cast<USoundWave>(LoadedAsset))
	{
		OutSummary = FString::Printf(TEXT("SoundWave: %s\n"), *SoundWave->GetName());
		OutSummary += FString::Printf(TEXT("Duration: %.2fs\n"), SoundWave->Duration);
		OutSummary += FString::Printf(TEXT("Sample Rate: %d Hz\n"), (int32)SoundWave->GetSampleRateForCurrentPlatform());
		OutSummary += FString::Printf(TEXT("Channels: %d\n"), SoundWave->NumChannels);
		OutSummary += FString::Printf(TEXT("Looping: %s\n"), SoundWave->bLooping ? TEXT("Yes") : TEXT("No"));
	}
	else if (USoundCue* SoundCue = Cast<USoundCue>(LoadedAsset))
	{
		OutSummary = FString::Printf(TEXT("SoundCue: %s\n"), *SoundCue->GetName());
		OutSummary += FString::Printf(TEXT("Duration: %.2fs\n"), SoundCue->Duration);
		OutSummary += FString::Printf(TEXT("Max Distance: %.1f\n"), SoundCue->GetMaxDistance());
	}
	else if (UAnimSequence* AnimSeq = Cast<UAnimSequence>(LoadedAsset))
	{
		OutSummary = FString::Printf(TEXT("AnimSequence: %s\n"), *AnimSeq->GetName());
		OutSummary += FString::Printf(TEXT("Duration: %.2fs\n"), AnimSeq->GetPlayLength());
		OutSummary += FString::Printf(TEXT("Frame Count: %d\n"), AnimSeq->GetNumberOfSampledKeys());
		if (AnimSeq->GetSkeleton())
			OutSummary += FString::Printf(TEXT("Skeleton: %s\n"), *AnimSeq->GetSkeleton()->GetName());
		OutSummary += FString::Printf(TEXT("Rate Scale: %.2f\n"), AnimSeq->RateScale);
	}
	else if (UAnimMontage* Montage = Cast<UAnimMontage>(LoadedAsset))
	{
		OutSummary = FString::Printf(TEXT("AnimMontage: %s\n"), *Montage->GetName());
		OutSummary += FString::Printf(TEXT("Duration: %.2fs\n"), Montage->GetPlayLength());
		OutSummary += FString::Printf(TEXT("Sections: %d\n"), Montage->CompositeSections.Num());
		for (const FCompositeSection& Section : Montage->CompositeSections)
			OutSummary += FString::Printf(TEXT("  Section: %s\n"), *Section.SectionName.ToString());
		if (!Montage->SlotAnimTracks.IsEmpty())
			OutSummary += FString::Printf(TEXT("Slot: %s\n"), *Montage->SlotAnimTracks[0].SlotName.ToString());
	}
	else if (UBlendSpace* BlendSpace = Cast<UBlendSpace>(LoadedAsset))
	{
		OutSummary = FString::Printf(TEXT("BlendSpace: %s\n"), *BlendSpace->GetName());
		OutSummary += FString::Printf(TEXT("Sample Count: %d\n"), BlendSpace->GetBlendSamples().Num());
	}
		else if (UCurveFloat* CurveFloat = Cast<UCurveFloat>(LoadedAsset))
	{
		OutSummary = FString::Printf(TEXT("CurveFloat: %s\n"), *CurveFloat->GetName());
		OutSummary += FString::Printf(TEXT("Key Count: %d\n"), CurveFloat->FloatCurve.GetNumKeys());
		if (CurveFloat->FloatCurve.GetNumKeys() > 0)
		{
			float MinTime, MaxTime;
			CurveFloat->FloatCurve.GetTimeRange(MinTime, MaxTime);
			float MinVal, MaxVal;
			CurveFloat->FloatCurve.GetValueRange(MinVal, MaxVal);
			OutSummary += FString::Printf(TEXT("Time Range: %.2f - %.2f\nValue Range: %.2f - %.2f\n"), MinTime, MaxTime, MinVal, MaxVal);
		}
	}
	else if (UPhysicsAsset* PhysAsset = Cast<UPhysicsAsset>(LoadedAsset))
	{
		OutSummary = FString::Printf(TEXT("PhysicsAsset: %s\n"), *PhysAsset->GetName());
		OutSummary += FString::Printf(TEXT("Body Count: %d\n"), PhysAsset->SkeletalBodySetups.Num());
		OutSummary += FString::Printf(TEXT("Constraint Count: %d\n"), PhysAsset->ConstraintSetup.Num());
	}
	else if (UParticleSystem* PS = Cast<UParticleSystem>(LoadedAsset))
	{
		OutSummary = FString::Printf(TEXT("ParticleSystem: %s\n"), *PS->GetName());
		OutSummary += FString::Printf(TEXT("Emitter Count: %d\n"), PS->Emitters.Num());
	}
	else
	{
		OutSummary = FString::Printf(TEXT("Asset Type: %s\nName: %s\nUse the appropriate umbrella tool for this asset type (niagara, pcg, behavior_tree, sequencer, etc.)."), *LoadedAsset->GetClass()->GetName(), *LoadedAsset->GetName());
	}
		if (OutSummary.IsEmpty())
	{
		OutError = TEXT("The summarizer returned an empty string. The asset may be empty or corrupted.");
	}
}

static void WrapSummaryAsJson(const FString& SummaryText, FString& OutJsonString)
{
	TSharedRef<FJsonObject> Obj = MakeShared<FJsonObject>();
	Obj->SetBoolField(TEXT("success"), true);
	Obj->SetStringField(TEXT("summary"), SummaryText);
	OutJsonString.Reset();
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutJsonString);
	FJsonSerializer::Serialize(Obj, Writer);
}

void HandleGetAssetSummaryFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	FString AssetPath;
	if (Args.IsValid())
	{
		if (!Args->TryGetStringField(TEXT("asset_path"), AssetPath))
			Args->TryGetStringField(TEXT("blueprint_path"), AssetPath);
	}
	FString Summary;
	HandleGetAssetSummary(AssetPath, Summary, OutError);
	if (OutError.IsEmpty()) WrapSummaryAsJson(Summary, OutJsonString);
}

void HandleGetSelectedNodesFromArgs(const TSharedPtr<FJsonObject>& , FString& OutJsonString, FString& OutError)
{
	TSharedPtr<FJsonObject> ResultObject = MakeShareable(new FJsonObject);
	auto Emit = [&](bool bSuccess, const FString& ErrorText = FString())
	{
		ResultObject->SetBoolField(TEXT("success"), bSuccess);
		if (!ErrorText.IsEmpty())
			ResultObject->SetStringField(TEXT("error"), ErrorText);
		FString ResultString;
		TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&ResultString);
		FJsonSerializer::Serialize(ResultObject.ToSharedRef(), Writer);
		OutJsonString = ResultString;
		if (!bSuccess) OutError = ErrorText;
	};

	UAssetEditorSubsystem* AssetEditorSubsystem = GEditor ? GEditor->GetEditorSubsystem<UAssetEditorSubsystem>() : nullptr;
	if (!AssetEditorSubsystem)
	{
		Emit(false, TEXT("Could not get the Asset Editor Subsystem."));
		return;
	}

	IAssetEditorInstance* ActiveEditor = nullptr;
	double LastActivationTime = 0.0;
	for (UObject* Asset : AssetEditorSubsystem->GetAllEditedAssets())
	{
		for (IAssetEditorInstance* Editor : AssetEditorSubsystem->FindEditorsForAsset(Asset))
		{
			if (Editor && Editor->GetLastActivationTime() > LastActivationTime)
			{
				LastActivationTime = Editor->GetLastActivationTime();
				ActiveEditor = Editor;
			}
		}
	}

	if (!ActiveEditor)
	{
		Emit(false, TEXT("No active asset editor found. Please open a Blueprint, Material, or Behavior Tree editor."));
		return;
	}

	const FName EditorName = ActiveEditor->GetEditorName();
	TSet<UObject*> SelectedNodes;
	FString NodesDescription;

	if (EditorName == FName(TEXT("BlueprintEditor")) || EditorName == FName(TEXT("AnimationBlueprintEditor")))
	{
		FBlueprintEditor* BlueprintEditor = static_cast<FBlueprintEditor*>(ActiveEditor);
		SelectedNodes = BlueprintEditor->GetSelectedNodes();
		FBpGraphDescriber Describer;
		NodesDescription = Describer.Describe(SelectedNodes);
	}
	else if (EditorName == FName(TEXT("WidgetBlueprintEditor")))
	{
		FWidgetBlueprintEditor* WidgetEditor = static_cast<FWidgetBlueprintEditor*>(ActiveEditor);
		SelectedNodes = WidgetEditor->GetSelectedNodes();
		FBpGraphDescriber Describer;
		NodesDescription = Describer.Describe(SelectedNodes);
	}
	else if (EditorName == FName(TEXT("MaterialEditor")))
	{
		FMaterialEditor* MaterialEditor = static_cast<FMaterialEditor*>(ActiveEditor);
		SelectedNodes = MaterialEditor->GetSelectedNodes();
		FMaterialNodeDescriber Describer;
		NodesDescription = Describer.Describe(SelectedNodes);
	}
	else if (EditorName == FName(TEXT("Behavior Tree")))
	{
		FBehaviorTreeEditor* BehaviorTreeEditor = static_cast<FBehaviorTreeEditor*>(ActiveEditor);
		SelectedNodes = BehaviorTreeEditor->GetSelectedNodes();
		FBtGraphDescriber Describer;
		NodesDescription = Describer.DescribeSelection(SelectedNodes);
	}
	else
	{
		Emit(false, FString::Printf(TEXT("The active editor ('%s') is not a supported type. Supported: Blueprint, Material, Behavior Tree."), *EditorName.ToString()));
		return;
	}

	if (SelectedNodes.Num() == 0)
	{
		Emit(false, TEXT("No nodes are currently selected in the active graph editor. Please select some nodes first."));
		return;
	}

	ResultObject->SetNumberField(TEXT("node_count"), SelectedNodes.Num());
	ResultObject->SetStringField(TEXT("editor_type"), EditorName.ToString());
	ResultObject->SetStringField(TEXT("nodes_description"), NodesDescription);
	Emit(true);
}

}
