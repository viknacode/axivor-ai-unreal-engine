// Copyright 2026, BlueprintsLab, All rights reserved

#include "AssetReferenceManager.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Editor/EditorEngine.h"
#include "Blueprint/UserWidgetBlueprint.h"
#include "Engine/Blueprint.h"
#include "BehaviorTree/BehaviorTree.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstance.h"
#include "Engine/DataTable.h"
#include "Misc/EngineVersionComparison.h"
#if UE_VERSION_OLDER_THAN(5, 5, 0)
#include "Engine/UserDefinedStruct.h"
#else
#include "StructUtils/UserDefinedStruct.h"
#endif
#include "Engine/UserDefinedEnum.h"
#include "NiagaraSystem.h"
#include "NiagaraEmitter.h"
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
#include "PCGGraph.h"
#include "Curves/CurveFloat.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "Particles/ParticleSystem.h"
#include "StateTree.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Describers/BpGraphDescriber.h"
#include "Describers/BpSummarizer.h"
#include "Describers/BtGraphDescriber.h"
#include "Describers/MaterialGraphDescriber.h"
#include "EditorAssetLibrary.h"
#include "Misc/FileHelper.h"
#include "SUECPMainWidget.h"

FAssetReferenceManager& FAssetReferenceManager::Get()
{
	static FAssetReferenceManager Instance;
	return Instance;
}

FAssetReferenceManager::FAssetReferenceManager()
{
}

void FAssetReferenceManager::LinkAsset(const FString& AssetPath, const FString& DisplayLabel, EAssetRefType Type)
{
	FLinkedAsset NewLink;
	NewLink.LinkId = FGuid::NewGuid();
	NewLink.DisplayLabel = DisplayLabel;
	NewLink.FullAssetPath = AssetPath;
	NewLink.Category = Type;
	NewLink.SummaryText = GenerateAssetSummary(AssetPath, Type);

	LinkedAssets.Add(NewLink);
	NotifyChange();
}

FString FAssetReferenceManager::GenerateAssetSummary(const FString& AssetPath, EAssetRefType Type)
{
	if (Type == EAssetRefType::CppHeader || Type == EAssetRefType::CppSource)
	{
		FString AbsPath = AssetPath;
		if (FPaths::IsRelative(AbsPath))
		{
			AbsPath = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir(), AbsPath);
		}

		FString FileContent;
		if (!FFileHelper::LoadFileToString(FileContent, *AbsPath))
		{
			return FString::Printf(TEXT("Could not read file: %s"), *AbsPath);
		}

		if (FileContent.Len() > 16000)
		{
			FileContent = FileContent.Left(16000) + TEXT("\n// ... [file truncated]");
		}

		FString TypeLabel = (Type == EAssetRefType::CppHeader) ? TEXT("C++ Header") : TEXT("C++ Source");
		return FString::Printf(TEXT("%s: %s\n```cpp\n%s\n```"), *TypeLabel, *FPaths::GetCleanFilename(AbsPath), *FileContent);
	}

	UObject* LoadedAsset = UEditorAssetLibrary::LoadAsset(AssetPath);
	if (!LoadedAsset)
	{
		return FString::Printf(TEXT("Failed to load asset at: %s"), *AssetPath);
	}

	FString Summary;
	if (Type == EAssetRefType::Blueprint || Type == EAssetRefType::WidgetBlueprint || Type == EAssetRefType::AnimBlueprint)
	{
		if (UBlueprint* Blueprint = Cast<UBlueprint>(LoadedAsset))
		{
			Summary = FBpSummarizer().Summarize(Blueprint);
		}
	}
	else if (Type == EAssetRefType::BehaviorTree)
	{
		if (UBehaviorTree* BehaviorTree = Cast<UBehaviorTree>(LoadedAsset))
		{
			FBtGraphDescriber Describer;
			Summary = Describer.Describe(BehaviorTree);
		}
	}
	else if (Type == EAssetRefType::Material)
	{
		if (UMaterial* Material = Cast<UMaterial>(LoadedAsset))
		{
			FMaterialGraphDescriber Describer;
			Summary = Describer.Describe(Material);
		}
	}
	else if (Type == EAssetRefType::MaterialInstance)
	{
		if (UMaterialInstance* MaterialInstance = Cast<UMaterialInstance>(LoadedAsset))
		{
			Summary = FString::Printf(TEXT("Material Instance: %s\nParent: %s\n"),
				*MaterialInstance->GetName(),
				MaterialInstance->Parent ? *MaterialInstance->Parent->GetName() : TEXT("None"));
		}
	}
	else if (Type == EAssetRefType::DataTable)
	{
		if (UDataTable* DataTable = Cast<UDataTable>(LoadedAsset))
		{
			FString RowStructName = DataTable->GetRowStruct() ? DataTable->GetRowStruct()->GetName() : TEXT("Unknown");
			TArray<FName> RowNames = DataTable->GetRowNames();
			Summary = FString::Printf(TEXT("DataTable: %s\nRow Struct: %s\nRow Count: %d\n"), *DataTable->GetName(), *RowStructName, RowNames.Num());
			if (const UScriptStruct* RowStruct = DataTable->GetRowStruct())
			{
				Summary += TEXT("Columns:\n");
				for (TFieldIterator<FProperty> It(RowStruct); It; ++It)
					Summary += FString::Printf(TEXT("  %s: %s\n"), *It->GetName(), *It->GetCPPType());
			}
			if (RowNames.Num() > 0)
			{
				Summary += TEXT("Rows: ");
				for (int32 i = 0; i < FMath::Min(RowNames.Num(), 10); i++)
					Summary += RowNames[i].ToString() + TEXT(", ");
				if (RowNames.Num() > 10)
					Summary += FString::Printf(TEXT("... (%d more)"), RowNames.Num() - 10);
			}
		}
	}
	else if (Type == EAssetRefType::Struct)
	{
		if (UUserDefinedStruct* UDS = Cast<UUserDefinedStruct>(LoadedAsset))
		{
			Summary = FString::Printf(TEXT("UserDefinedStruct: %s\nMembers:\n"), *UDS->GetName());
			for (TFieldIterator<FProperty> It(UDS); It; ++It)
				Summary += FString::Printf(TEXT("  %s: %s\n"), *It->GetDisplayNameText().ToString(), *It->GetCPPType());
		}
	}
	else if (Type == EAssetRefType::Enum)
	{
		if (UUserDefinedEnum* UDE = Cast<UUserDefinedEnum>(LoadedAsset))
		{
			Summary = FString::Printf(TEXT("UserDefinedEnum: %s\nValues:\n"), *UDE->GetName());
			for (int32 i = 0; i < UDE->NumEnums() - 1; i++)
				Summary += FString::Printf(TEXT("  %s = %lld\n"), *UDE->GetNameStringByIndex(i), UDE->GetValueByIndex(i));
		}
	}
	else if (Type == EAssetRefType::NiagaraSystem)
	{
		if (UNiagaraSystem* NiagaraSys = Cast<UNiagaraSystem>(LoadedAsset))
		{
			Summary = FString::Printf(TEXT("NiagaraSystem: %s\n"), *NiagaraSys->GetName());
			const TArray<FNiagaraEmitterHandle>& Handles = NiagaraSys->GetEmitterHandles();
			Summary += FString::Printf(TEXT("Emitter Count: %d\n"), Handles.Num());
			for (const FNiagaraEmitterHandle& Handle : Handles)
			{
				Summary += FString::Printf(TEXT("  Emitter: %s (Enabled: %s)\n"),
					*Handle.GetName().ToString(),
					Handle.GetIsEnabled() ? TEXT("Yes") : TEXT("No"));
			}
			Summary += FString::Printf(TEXT("Fixed Bounds: %s\n"), NiagaraSys->bFixedBounds ? TEXT("Yes") : TEXT("No"));
		}
	}
	else if (Type == EAssetRefType::LevelSequence)
	{
		if (ULevelSequence* Sequence = Cast<ULevelSequence>(LoadedAsset))
		{
			Summary = FString::Printf(TEXT("LevelSequence: %s\n"), *Sequence->GetName());
			if (UMovieScene* MovieScene = Sequence->GetMovieScene())
			{
				Summary += FString::Printf(TEXT("Tracks: %d\n"), MovieScene->GetTracks().Num());
				for (UMovieSceneTrack* Track : MovieScene->GetTracks())
				{
					if (Track)
						Summary += FString::Printf(TEXT("  Track: %s (%s)\n"), *Track->GetDisplayName().ToString(), *Track->GetClass()->GetName());
				}
				FFrameRate TickRes = MovieScene->GetTickResolution();
				FFrameRate DisplayRate = MovieScene->GetDisplayRate();
				TRange<FFrameNumber> PlayRange = MovieScene->GetPlaybackRange();
				double StartSec = TickRes.AsSeconds(PlayRange.GetLowerBoundValue());
				double EndSec = TickRes.AsSeconds(PlayRange.GetUpperBoundValue());
				Summary += FString::Printf(TEXT("Duration: %.2fs (%.1f - %.1f)\nDisplay Rate: %d fps\n"), EndSec - StartSec, StartSec, EndSec, DisplayRate.Numerator);
				Summary += FString::Printf(TEXT("Bindings: %d\n"), MovieScene->GetPossessableCount() + MovieScene->GetSpawnableCount());
			}
		}
	}
	else if (Type == EAssetRefType::StaticMesh)
	{
		if (UStaticMesh* Mesh = Cast<UStaticMesh>(LoadedAsset))
		{
			Summary = FString::Printf(TEXT("StaticMesh: %s\n"), *Mesh->GetName());
			Summary += FString::Printf(TEXT("LOD Count: %d\n"), Mesh->GetNumLODs());
			if (Mesh->GetRenderData() && Mesh->GetRenderData()->LODResources.Num() > 0)
			{
				const FStaticMeshLODResources& LOD0 = Mesh->GetRenderData()->LODResources[0];
				Summary += FString::Printf(TEXT("Triangles (LOD0): %d\nVertices (LOD0): %d\n"), LOD0.GetNumTriangles(), LOD0.GetNumVertices());
			}
			Summary += FString::Printf(TEXT("Material Slots: %d\n"), Mesh->GetStaticMaterials().Num());
			for (int32 i = 0; i < Mesh->GetStaticMaterials().Num(); i++)
			{
				const FStaticMaterial& Mat = Mesh->GetStaticMaterials()[i];
				Summary += FString::Printf(TEXT("  [%d] %s\n"), i, Mat.MaterialInterface ? *Mat.MaterialInterface->GetName() : TEXT("None"));
			}
			FBoxSphereBounds Bounds = Mesh->GetBounds();
			Summary += FString::Printf(TEXT("Bounds: (%.1f, %.1f, %.1f)\n"), Bounds.BoxExtent.X * 2, Bounds.BoxExtent.Y * 2, Bounds.BoxExtent.Z * 2);
		}
	}
	else if (Type == EAssetRefType::SkeletalMesh)
	{
		if (USkeletalMesh* Mesh = Cast<USkeletalMesh>(LoadedAsset))
		{
			Summary = FString::Printf(TEXT("SkeletalMesh: %s\n"), *Mesh->GetName());
			if (const FSkeletalMeshRenderData* RenderData = Mesh->GetResourceForRendering())
			{
				Summary += FString::Printf(TEXT("LOD Count: %d\n"), RenderData->LODRenderData.Num());
				if (RenderData->LODRenderData.Num() > 0)
				{
					Summary += FString::Printf(TEXT("Vertices (LOD0): %d\n"), RenderData->LODRenderData[0].GetNumVertices());
				}
			}
			if (Mesh->GetSkeleton())
			{
				Summary += FString::Printf(TEXT("Skeleton: %s\n"), *Mesh->GetSkeleton()->GetName());
				Summary += FString::Printf(TEXT("Bone Count: %d\n"), Mesh->GetRefSkeleton().GetNum());
			}
			Summary += FString::Printf(TEXT("Material Slots: %d\n"), Mesh->GetMaterials().Num());
			if (Mesh->GetMorphTargets().Num() > 0)
				Summary += FString::Printf(TEXT("Morph Targets: %d\n"), Mesh->GetMorphTargets().Num());
		}
	}
	else if (Type == EAssetRefType::Texture)
	{
		if (UTexture2D* Texture = Cast<UTexture2D>(LoadedAsset))
		{
			Summary = FString::Printf(TEXT("Texture2D: %s\n"), *Texture->GetName());
			Summary += FString::Printf(TEXT("Resolution: %dx%d\n"), Texture->GetSizeX(), Texture->GetSizeY());
			Summary += FString::Printf(TEXT("Format: %s\n"), GetPixelFormatString(Texture->GetPixelFormat()));
			Summary += FString::Printf(TEXT("SRGB: %s\n"), Texture->SRGB ? TEXT("Yes") : TEXT("No"));
			Summary += FString::Printf(TEXT("Compression: %s\n"), *UEnum::GetValueAsString(Texture->CompressionSettings));
			Summary += FString::Printf(TEXT("Mip Count: %d\n"), Texture->GetNumMips());
		}
	}
	else if (Type == EAssetRefType::Sound)
	{
		if (USoundWave* SoundWave = Cast<USoundWave>(LoadedAsset))
		{
			Summary = FString::Printf(TEXT("SoundWave: %s\n"), *SoundWave->GetName());
			Summary += FString::Printf(TEXT("Duration: %.2fs\n"), SoundWave->Duration);
			Summary += FString::Printf(TEXT("Sample Rate: %d Hz\n"), (int32)SoundWave->GetSampleRateForCurrentPlatform());
			Summary += FString::Printf(TEXT("Channels: %d\n"), SoundWave->NumChannels);
			Summary += FString::Printf(TEXT("Looping: %s\n"), SoundWave->bLooping ? TEXT("Yes") : TEXT("No"));
		}
		else if (USoundCue* SoundCue = Cast<USoundCue>(LoadedAsset))
		{
			Summary = FString::Printf(TEXT("SoundCue: %s\n"), *SoundCue->GetName());
			Summary += FString::Printf(TEXT("Duration: %.2fs\n"), SoundCue->Duration);
			Summary += FString::Printf(TEXT("Max Distance: %.1f\n"), SoundCue->GetMaxDistance());
		}
	}
	else if (Type == EAssetRefType::Animation)
	{
		if (UAnimSequence* AnimSeq = Cast<UAnimSequence>(LoadedAsset))
		{
			Summary = FString::Printf(TEXT("AnimSequence: %s\n"), *AnimSeq->GetName());
			Summary += FString::Printf(TEXT("Duration: %.2fs\n"), AnimSeq->GetPlayLength());
			Summary += FString::Printf(TEXT("Frame Count: %d\n"), AnimSeq->GetNumberOfSampledKeys());
			if (AnimSeq->GetSkeleton())
				Summary += FString::Printf(TEXT("Skeleton: %s\n"), *AnimSeq->GetSkeleton()->GetName());
			Summary += FString::Printf(TEXT("Rate Scale: %.2f\n"), AnimSeq->RateScale);
		}
		else if (UAnimMontage* Montage = Cast<UAnimMontage>(LoadedAsset))
		{
			Summary = FString::Printf(TEXT("AnimMontage: %s\n"), *Montage->GetName());
			Summary += FString::Printf(TEXT("Duration: %.2fs\n"), Montage->GetPlayLength());
			Summary += FString::Printf(TEXT("Sections: %d\n"), Montage->CompositeSections.Num());
			for (const FCompositeSection& Section : Montage->CompositeSections)
				Summary += FString::Printf(TEXT("  Section: %s\n"), *Section.SectionName.ToString());
			if (!Montage->SlotAnimTracks.IsEmpty())
				Summary += FString::Printf(TEXT("Slot: %s\n"), *Montage->SlotAnimTracks[0].SlotName.ToString());
		}
		else if (UBlendSpace* BlendSpace = Cast<UBlendSpace>(LoadedAsset))
		{
			Summary = FString::Printf(TEXT("BlendSpace: %s\n"), *BlendSpace->GetName());
			Summary += FString::Printf(TEXT("Sample Count: %d\n"), BlendSpace->GetBlendSamples().Num());
		}
	}
	else if (Type == EAssetRefType::PCGGraph)
	{
		if (UPCGGraph* Graph = Cast<UPCGGraph>(LoadedAsset))
		{
			Summary = FString::Printf(TEXT("PCGGraph: %s\n"), *Graph->GetName());
			Summary += FString::Printf(TEXT("Node Count: %d\n"), Graph->GetNodes().Num());
		}
	}
	else if (Type == EAssetRefType::Curve)
	{
		if (UCurveFloat* CurveFloat = Cast<UCurveFloat>(LoadedAsset))
		{
			Summary = FString::Printf(TEXT("CurveFloat: %s\n"), *CurveFloat->GetName());
			Summary += FString::Printf(TEXT("Key Count: %d\n"), CurveFloat->FloatCurve.GetNumKeys());
			if (CurveFloat->FloatCurve.GetNumKeys() > 0)
			{
				float MinTime, MaxTime;
				CurveFloat->FloatCurve.GetTimeRange(MinTime, MaxTime);
				float MinVal, MaxVal;
				CurveFloat->FloatCurve.GetValueRange(MinVal, MaxVal);
				Summary += FString::Printf(TEXT("Time Range: %.2f - %.2f\nValue Range: %.2f - %.2f\n"), MinTime, MaxTime, MinVal, MaxVal);
			}
		}
	}
	else if (Type == EAssetRefType::PhysicsAsset)
	{
		if (UPhysicsAsset* PhysAsset = Cast<UPhysicsAsset>(LoadedAsset))
		{
			Summary = FString::Printf(TEXT("PhysicsAsset: %s\n"), *PhysAsset->GetName());
			Summary += FString::Printf(TEXT("Body Count: %d\n"), PhysAsset->SkeletalBodySetups.Num());
			Summary += FString::Printf(TEXT("Constraint Count: %d\n"), PhysAsset->ConstraintSetup.Num());
		}
	}
	else if (Type == EAssetRefType::StateTree)
	{
		Summary = FString::Printf(TEXT("StateTree: %s\n"), *LoadedAsset->GetName());
	}
	else if (Type == EAssetRefType::ParticleSystem)
	{
		if (UParticleSystem* PS = Cast<UParticleSystem>(LoadedAsset))
		{
			Summary = FString::Printf(TEXT("ParticleSystem: %s\n"), *PS->GetName());
			Summary += FString::Printf(TEXT("Emitter Count: %d\n"), PS->Emitters.Num());
		}
	}

	if (Summary.IsEmpty())
	{
		Summary = FString::Printf(TEXT("Asset: %s (Type: %s)\n"), *LoadedAsset->GetName(), *LoadedAsset->GetClass()->GetName());
	}

	return Summary;
}

void FAssetReferenceManager::UnlinkAsset(const FGuid& LinkId)
{
	LinkedAssets.RemoveAll([&](const FLinkedAsset& Link) { return Link.LinkId == LinkId; });
	NotifyChange();
}

void FAssetReferenceManager::UnlinkAll()
{
	LinkedAssets.Empty();
	NotifyChange();
}

TArray<FLinkedAsset> FAssetReferenceManager::GetLinkedAssets() const
{
	return LinkedAssets;
}

FString FAssetReferenceManager::GetSummaryForAI() const
{
	if (LinkedAssets.Num() == 0)
	{
		return FString();
	}

	static constexpr int32 MaxCharsPerAsset = 16000;
	static constexpr int32 MaxTotalChars = 60000;

	FString Summary = TEXT("=== REFERENCED ASSETS ===\n\n");
	int32 TotalCharsUsed = Summary.Len();

	for (const FLinkedAsset& Link : LinkedAssets)
	{
		FString TypeName;
		switch (Link.Category)
		{
		case EAssetRefType::Blueprint: TypeName = TEXT("Blueprint"); break;
		case EAssetRefType::WidgetBlueprint: TypeName = TEXT("Widget Blueprint"); break;
		case EAssetRefType::AnimBlueprint: TypeName = TEXT("Anim Blueprint"); break;
		case EAssetRefType::BehaviorTree: TypeName = TEXT("Behavior Tree"); break;
		case EAssetRefType::Material: TypeName = TEXT("Material"); break;
		case EAssetRefType::MaterialInstance: TypeName = TEXT("Material Instance"); break;
		case EAssetRefType::DataTable: TypeName = TEXT("Data Table"); break;
		case EAssetRefType::Struct: TypeName = TEXT("Struct"); break;
		case EAssetRefType::Enum: TypeName = TEXT("Enum"); break;
		case EAssetRefType::NiagaraSystem: TypeName = TEXT("Niagara System"); break;
		case EAssetRefType::LevelSequence: TypeName = TEXT("Level Sequence"); break;
		case EAssetRefType::StaticMesh: TypeName = TEXT("Static Mesh"); break;
		case EAssetRefType::SkeletalMesh: TypeName = TEXT("Skeletal Mesh"); break;
		case EAssetRefType::Texture: TypeName = TEXT("Texture"); break;
		case EAssetRefType::Sound: TypeName = TEXT("Sound"); break;
		case EAssetRefType::Animation: TypeName = TEXT("Animation"); break;
		case EAssetRefType::PCGGraph: TypeName = TEXT("PCG Graph"); break;
		case EAssetRefType::PhysicsAsset: TypeName = TEXT("Physics Asset"); break;
		case EAssetRefType::StateTree: TypeName = TEXT("State Tree"); break;
		case EAssetRefType::Curve: TypeName = TEXT("Curve"); break;
		case EAssetRefType::ParticleSystem: TypeName = TEXT("Particle System"); break;
		case EAssetRefType::CppHeader: TypeName = TEXT("C++ Header"); break;
		case EAssetRefType::CppSource: TypeName = TEXT("C++ Source"); break;
		default: TypeName = TEXT("Asset"); break;
		}

		FString SummaryText = Link.SummaryText;
		if (SummaryText.Len() > MaxCharsPerAsset)
		{
			SummaryText = SummaryText.Left(MaxCharsPerAsset) + TEXT("\n... [summary truncated]\n");
		}

		FString AssetEntry = FString::Printf(TEXT("### [%s] %s\nPath: %s\n\n%s\n\n"),
			*TypeName, *Link.DisplayLabel, *Link.FullAssetPath, *SummaryText);

		if (TotalCharsUsed + AssetEntry.Len() > MaxTotalChars)
		{
			Summary += TEXT("... [additional referenced assets omitted due to size]\n");
			break;
		}

		Summary += AssetEntry;
		TotalCharsUsed += AssetEntry.Len();
	}

	return Summary;
}

FString FAssetReferenceManager::FormatAsMarkdown() const
{
	if (LinkedAssets.Num() == 0)
	{
		return FString();
	}

	FString Markdown = TEXT("## Referenced Assets\n\n");

	for (const FLinkedAsset& Link : LinkedAssets)
	{
		Markdown += FString::Printf(TEXT("- **%s** (`%s`)\n"), *Link.DisplayLabel, *Link.FullAssetPath);
	}

	return Markdown;
}

void FAssetReferenceManager::NotifyChange()
{
	OnLinksChanged.Broadcast();
}
