// Copyright 2026, BlueprintsLab, All rights reserved

#include "FUECPScannerCoordinator.h"
#include "SUECPMainWidget.h"
#include "Widget/UUECPAppBridge.h"
#include "Managers/ChatHistoryManager.h"
#include "Managers/EditorProfileSync.h"
#include "Managers/FreeTierConfigManager.h"
#include "Managers/HttpCommunicationManager.h"
#include "Managers/SettingsManager.h"
#include "ApiKeyManager.h"
#include "AssetReferenceManager.h"
#include "AgentRunnerTypes.h"
#include "UECPCoreModule.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonReader.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/ConfigCacheIni.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "Async/Async.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Engine/Blueprint.h"
#include "Engine/DataAsset.h"
#include "Engine/DataTable.h"
#include "Engine/UserDefinedEnum.h"
#include "Misc/EngineVersionComparison.h"
#if UE_VERSION_OLDER_THAN(5, 5, 0)
#include "Engine/UserDefinedStruct.h"
#else
#include "StructUtils/UserDefinedStruct.h"
#endif
#include "Engine/World.h"
#include "Materials/MaterialInterface.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstance.h"
#include "Engine/Texture.h"
#include "Engine/Texture2D.h"
#include "Engine/StaticMesh.h"
#include "Engine/SkeletalMesh.h"
#include "BehaviorTree/BehaviorTree.h"
#include "Kismet2/StructureEditorUtils.h"
#include "UserDefinedStructure/UserDefinedStructEditorData.h"
#include "AssetCompilingManager.h"
#include "Describers/BpIssue.h"
#include "Describers/BpSummarizer.h"
#include "Describers/BtGraphDescriber.h"
#include "HAL/FileManager.h"
#include "Interfaces/IPluginManager.h"
#include "ScannerQueryEngine.h"
#include "Services/ProjectIndexSchema.h"
#include "SUECPMainWidget.h"
#include "UECPCoreModule.h"

namespace
{
	TSharedPtr<FJsonObject> MakeIndexEntry(const FString& Type, const FString& Summary)
	{
		TSharedPtr<FJsonObject> Entry = MakeShareable(new FJsonObject);
		Entry->SetStringField(UECPProjectIndex::FieldType, Type);
		Entry->SetStringField(UECPProjectIndex::FieldSummary, Summary);
		return Entry;
	}

	FString DeriveShortSummary(const FString& FullSummary)
	{
		FString Trimmed = FullSummary;
		const int32 HeaderEnd = Trimmed.Find(TEXT("\n"));
		if (HeaderEnd != INDEX_NONE && Trimmed.StartsWith(TEXT("---")))
		{
			Trimmed = Trimmed.RightChop(HeaderEnd + 1);
		}
		Trimmed.ReplaceInline(TEXT("\n"), TEXT(" "));
		Trimmed.TrimStartAndEndInline();
		if (Trimmed.Len() > 200) Trimmed = Trimmed.Left(197) + TEXT("...");
		return Trimmed;
	}

	void AttachIssuesToEntry(const TSharedRef<FJsonObject>& Entry, const TArray<FBpIssue>& Issues)
	{
		if (Issues.Num() == 0) return;
		TArray<TSharedPtr<FJsonValue>> IssueValues;
		IssueValues.Reserve(Issues.Num());
		for (const FBpIssue& Issue : Issues)
		{
			TSharedPtr<FJsonObject> IssueObj = MakeShareable(new FJsonObject);
			IssueObj->SetStringField(UECPProjectIndex::IssueSeverity, FBpIssue::SeverityToString(Issue.Severity));
			IssueObj->SetStringField(UECPProjectIndex::IssueCode, Issue.Code);
			IssueObj->SetStringField(UECPProjectIndex::IssueMessage, Issue.Message);
			IssueValues.Add(MakeShareable(new FJsonValueObject(IssueObj)));
		}
		Entry->SetArrayField(UECPProjectIndex::FieldIssues, IssueValues);
	}

	int64 GetAssetMtime(const FAssetData& Data)
	{
		const FString PkgFile = FPackageName::LongPackageNameToFilename(
			Data.PackageName.ToString(), FPackageName::GetAssetPackageExtension());
		const FDateTime Stamp = IFileManager::Get().GetTimeStamp(*PkgFile);
		return Stamp == FDateTime::MinValue() ? 0 : Stamp.ToUnixTimestamp();
	}

	void SummarizeTexture(UTexture* Texture, FString& OutSummary, TArray<FBpIssue>& OutIssues, int32& OutComplexity)
	{
		if (!Texture) return;
		TStringBuilder<1024> S;
		S.Appendf(TEXT("--- TEXTURE SUMMARY ---\nName: %s\n"), *Texture->GetName());

		const FString LodGroupName = StaticEnum<TextureGroup>()
			? StaticEnum<TextureGroup>()->GetNameStringByValue(Texture->LODGroup)
			: FString::FromInt(Texture->LODGroup);
		const FString CompName = StaticEnum<TextureCompressionSettings>()
			? StaticEnum<TextureCompressionSettings>()->GetNameStringByValue(Texture->CompressionSettings)
			: FString::FromInt(Texture->CompressionSettings);
		S.Appendf(TEXT("LOD Group: %s\nCompression: %s\n"), *LodGroupName, *CompName);
		S.Appendf(TEXT("sRGB: %s\n"), Texture->SRGB ? TEXT("true") : TEXT("false"));
		S.Appendf(TEXT("Max Texture Size: %d\n"), Texture->MaxTextureSize);

		int32 W = 0, H = 0;
		if (UTexture2D* Tex2D = Cast<UTexture2D>(Texture))
		{
			W = Tex2D->GetSizeX();
			H = Tex2D->GetSizeY();
			S.Appendf(TEXT("Resolution: %dx%d\n"), W, H);
			S.Appendf(TEXT("Mip Count: %d\n"), Tex2D->GetNumMips());
		}

		const int32 LargerDim = FMath::Max(W, H);
		OutComplexity = LargerDim;

		if (LargerDim > 4096)
		{
			OutIssues.Add({ FBpIssue::ESeverity::Warn, TEXT("tex_oversized"),
				FString::Printf(TEXT("Texture is %dx%d — > 4K is rarely needed outside hero art. Bump MaxTextureSize down if used at small screen size."), W, H) });
		}
		if (Texture->LODGroup == TEXTUREGROUP_UI && LargerDim > 2048)
		{
			OutIssues.Add({ FBpIssue::ESeverity::Warn, TEXT("tex_ui_oversized"),
				FString::Printf(TEXT("UI texture %dx%d — UI rarely needs > 2K."), W, H) });
		}
		if (Texture->CompressionSettings == TC_VectorDisplacementmap
			|| Texture->CompressionSettings == TC_HDR
			|| Texture->CompressionSettings == TC_EditorIcon)
		{
			OutIssues.Add({ FBpIssue::ESeverity::Info, TEXT("tex_uncompressed_or_specialty"),
				FString::Printf(TEXT("Compression setting `%s` is uncompressed or specialty — verify this is intended (high memory cost)."), *CompName) });
		}
		if (Texture->LODGroup == TEXTUREGROUP_World && LargerDim < 256)
		{
			OutIssues.Add({ FBpIssue::ESeverity::Info, TEXT("tex_tiny_world"),
				FString::Printf(TEXT("World texture is %dx%d — likely a sliver of the atlas budget; consider an atlas."), W, H) });
		}

		OutSummary = FString(S);
	}

	void SummarizeStaticMesh(UStaticMesh* Mesh, FString& OutSummary, TArray<FBpIssue>& OutIssues, int32& OutComplexity)
	{
		if (!Mesh) return;
		TStringBuilder<1024> S;
		S.Appendf(TEXT("--- STATIC MESH SUMMARY ---\nName: %s\n"), *Mesh->GetName());

		const int32 NumLODs    = Mesh->GetNumLODs();
		const int32 NumSources = Mesh->GetNumSourceModels();
		const int32 NumMats    = Mesh->GetStaticMaterials().Num();
		const bool  bHasCollision = (Mesh->GetBodySetup() != nullptr);

		int32 Tris = 0;
		if (NumLODs > 0)
		{
			Tris = Mesh->GetNumTriangles(0);
		}

		S.Appendf(TEXT("Triangles (LOD0): %d\n"), Tris);
		S.Appendf(TEXT("LOD Count: %d\nSource Models: %d\n"), NumLODs, NumSources);
		S.Appendf(TEXT("Material Slots: %d\n"), NumMats);
		S.Appendf(TEXT("Has Body Setup (Collision): %s\n"), bHasCollision ? TEXT("true") : TEXT("false"));

		OutComplexity = Tris;

		if (Tris > 10000 && NumLODs <= 1)
		{
			OutIssues.Add({ FBpIssue::ESeverity::Warn, TEXT("mesh_high_tri_no_lod"),
				FString::Printf(TEXT("%d triangles at LOD0 with only %d LOD(s) — add LODs to keep render cost in check at distance."), Tris, NumLODs) });
		}
		if (Tris > 100000)
		{
			OutIssues.Add({ FBpIssue::ESeverity::Critical, TEXT("mesh_extreme_tri_count"),
				FString::Printf(TEXT("%d triangles — extreme density; consider a high-poly source + Nanite or a bake."), Tris) });
		}
		if (!bHasCollision)
		{
			OutIssues.Add({ FBpIssue::ESeverity::Info, TEXT("mesh_no_collision"),
				TEXT("No body setup — mesh has no collision. Intended for decoration?") });
		}
		if (NumMats > 8)
		{
			OutIssues.Add({ FBpIssue::ESeverity::Warn, TEXT("mesh_many_material_slots"),
				FString::Printf(TEXT("%d material slots — each is a draw call. Consider merging materials or splitting the mesh."), NumMats) });
		}

		OutSummary = FString(S);
	}

	void SummarizeSkeletalMesh(USkeletalMesh* Mesh, FString& OutSummary, TArray<FBpIssue>& OutIssues, int32& OutComplexity)
	{
		if (!Mesh) return;
		TStringBuilder<1024> S;
		S.Appendf(TEXT("--- SKELETAL MESH SUMMARY ---\nName: %s\n"), *Mesh->GetName());

		const int32 NumBones = Mesh->GetRefSkeleton().GetNum();
		const int32 NumLODs  = Mesh->GetLODNum();
		const int32 NumMats  = Mesh->GetMaterials().Num();
		const int32 NumMorph = Mesh->GetMorphTargets().Num();

		S.Appendf(TEXT("Bones: %d\nLOD Count: %d\nMaterial Slots: %d\nMorph Targets: %d\n"),
			NumBones, NumLODs, NumMats, NumMorph);

		OutComplexity = NumBones;

		if (NumBones > 300)
		{
			OutIssues.Add({ FBpIssue::ESeverity::Warn, TEXT("skel_high_bone_count"),
				FString::Printf(TEXT("%d bones — anim sample cost scales with bone count. Consider a simpler rig for background characters."), NumBones) });
		}
		if (NumLODs <= 1)
		{
			OutIssues.Add({ FBpIssue::ESeverity::Info, TEXT("skel_no_lods"),
				TEXT("Single LOD — at distance the full skinning cost is paid. Add LODs for any skeletal mesh visible at range.") });
		}
		if (NumMats > 8)
		{
			OutIssues.Add({ FBpIssue::ESeverity::Warn, TEXT("skel_many_material_slots"),
				FString::Printf(TEXT("%d material slots — each is a draw call per visible LOD."), NumMats) });
		}
		if (NumMorph > 50)
		{
			OutIssues.Add({ FBpIssue::ESeverity::Info, TEXT("skel_many_morphs"),
				FString::Printf(TEXT("%d morph targets — high count may indicate a face rig; verify only the in-use targets are active."), NumMorph) });
		}

		OutSummary = FString(S);
	}

	void SummarizeMaterial(UMaterialInterface* MatInterface, FString& OutSummary, TArray<FBpIssue>& OutIssues, int32& OutComplexity)
	{
		if (!MatInterface) return;
		TStringBuilder<1024> S;
		S.Appendf(TEXT("--- MATERIAL SUMMARY ---\nName: %s\nClass: %s\n"),
			*MatInterface->GetName(), *MatInterface->GetClass()->GetName());

		const EBlendMode Blend = MatInterface->GetBlendMode();
		const FMaterialShadingModelField ShadingModels = MatInterface->GetShadingModels();
		const bool bTwoSided = MatInterface->IsTwoSided();

		const FString BlendName = StaticEnum<EBlendMode>()
			? StaticEnum<EBlendMode>()->GetNameStringByValue(Blend)
			: FString::FromInt((int32)Blend);
		S.Appendf(TEXT("Blend Mode: %s\nTwo Sided: %s\n"),
			*BlendName, bTwoSided ? TEXT("true") : TEXT("false"));

		TArray<FString> ShadingList;
		if (ShadingModels.HasShadingModel(MSM_Unlit))        ShadingList.Add(TEXT("Unlit"));
		if (ShadingModels.HasShadingModel(MSM_DefaultLit))   ShadingList.Add(TEXT("DefaultLit"));
		if (ShadingModels.HasShadingModel(MSM_Subsurface))   ShadingList.Add(TEXT("Subsurface"));
		if (ShadingModels.HasShadingModel(MSM_PreintegratedSkin)) ShadingList.Add(TEXT("PreintegratedSkin"));
		if (ShadingModels.HasShadingModel(MSM_ClearCoat))    ShadingList.Add(TEXT("ClearCoat"));
		if (ShadingModels.HasShadingModel(MSM_TwoSidedFoliage)) ShadingList.Add(TEXT("TwoSidedFoliage"));
		if (ShadingModels.HasShadingModel(MSM_Hair))         ShadingList.Add(TEXT("Hair"));
		if (ShadingModels.HasShadingModel(MSM_Cloth))        ShadingList.Add(TEXT("Cloth"));
		if (ShadingModels.HasShadingModel(MSM_Eye))          ShadingList.Add(TEXT("Eye"));
		if (ShadingModels.HasShadingModel(MSM_SingleLayerWater)) ShadingList.Add(TEXT("SingleLayerWater"));
		if (ShadingList.Num() > 0)
		{
			S.Appendf(TEXT("Shading: %s\n"), *FString::Join(ShadingList, TEXT(", ")));
		}

		int32 ExprCount = 0;
		if (UMaterialInstance* MI = Cast<UMaterialInstance>(MatInterface))
		{
			if (MI->Parent) S.Appendf(TEXT("Parent: %s\n"), *MI->Parent->GetName());
#if WITH_EDITORONLY_DATA
			S.Appendf(TEXT("Scalar Params: %d\nVector Params: %d\nTexture Params: %d\n"),
				MI->ScalarParameterValues.Num(),
				MI->VectorParameterValues.Num(),
				MI->TextureParameterValues.Num());
#endif
		}
		else if (UMaterial* Mat = Cast<UMaterial>(MatInterface))
		{
#if WITH_EDITORONLY_DATA
			ExprCount = Mat->GetExpressions().Num();
			S.Appendf(TEXT("Expression Nodes: %d\n"), ExprCount);
#endif
		}

		OutComplexity = ExprCount;

		const bool bTranslucent = (Blend == BLEND_Translucent || Blend == BLEND_AlphaComposite || Blend == BLEND_AlphaHoldout);
		const bool bIsLit       = !ShadingModels.HasShadingModel(MSM_Unlit) && ShadingList.Num() > 0;
		if (bTranslucent && bIsLit)
		{
			OutIssues.Add({ FBpIssue::ESeverity::Warn, TEXT("mat_translucent_lit"),
				TEXT("Translucent blend + lit shading is expensive — confirm this surface really needs full lighting.") });
		}
		if (bTwoSided && Blend == BLEND_Opaque)
		{
			OutIssues.Add({ FBpIssue::ESeverity::Info, TEXT("mat_two_sided_opaque"),
				TEXT("Two-sided opaque doubles fragment cost on visible faces. Use only if backfaces are actually viewed.") });
		}
		if (ExprCount > 150)
		{
			OutIssues.Add({ FBpIssue::ESeverity::Warn, TEXT("mat_complex_graph"),
				FString::Printf(TEXT("%d expression nodes — complex material. Heavy permutation cost; check shader compile times."), ExprCount) });
		}
		if (ExprCount > 300)
		{
			OutIssues.Add({ FBpIssue::ESeverity::Critical, TEXT("mat_extreme_graph"),
				FString::Printf(TEXT("%d expression nodes — extreme complexity. Consider material functions / instance overrides."), ExprCount) });
		}

		OutSummary = FString(S);
	}

	void WriteScanSnapshot(const TSharedPtr<FJsonObject>& IndexObject)
	{
		if (!IndexObject.IsValid()) return;

		const FString Dir = FPaths::ProjectSavedDir() / TEXT("AI") / TEXT("scan_snapshots");
		IFileManager::Get().MakeDirectory(*Dir,  true);

		TMap<FString, int32> IssueCountsByCode;
		int32 Critical = 0, Warn = 0, Info = 0, AssetsWithIssues = 0;
		for (const auto& Pair : IndexObject->Values)
		{
			const FString Key(*Pair.Key);
			if (Key.StartsWith(TEXT("_"))) continue;
			const TSharedPtr<FJsonObject>* Entry = nullptr;
			if (!Pair.Value.IsValid() || !Pair.Value->TryGetObject(Entry) || !Entry || !Entry->IsValid()) continue;

			const TArray<TSharedPtr<FJsonValue>>* Issues = nullptr;
			if (!(*Entry)->TryGetArrayField(UECPProjectIndex::FieldIssues, Issues) || !Issues || Issues->Num() == 0) continue;
			AssetsWithIssues++;
			for (const TSharedPtr<FJsonValue>& IV : *Issues)
			{
				if (!IV.IsValid()) continue;
				const TSharedPtr<FJsonObject>* IO = nullptr;
				if (!IV->TryGetObject(IO) || !IO || !IO->IsValid()) continue;
				FString Sev, Code;
				(*IO)->TryGetStringField(UECPProjectIndex::IssueSeverity, Sev);
				(*IO)->TryGetStringField(UECPProjectIndex::IssueCode, Code);
				IssueCountsByCode.FindOrAdd(Code)++;
				if (Sev == TEXT("critical")) Critical++;
				else if (Sev == TEXT("warn")) Warn++;
				else                          Info++;
			}
		}
		const double Health = FMath::Clamp(
			10.0 - (Critical * 1.0) - (Warn * 0.2) - (Info * 0.05),
			0.0, 10.0);

		TSharedPtr<FJsonObject> Snap = MakeShareable(new FJsonObject);
		const FDateTime Now = FDateTime::UtcNow();
		Snap->SetStringField(TEXT("timestamp"), Now.ToString());

		const TSharedPtr<FJsonObject>* CountsObj = nullptr;
		if (IndexObject->TryGetObjectField(UECPProjectIndex::MetaCounts, CountsObj) && CountsObj && CountsObj->IsValid())
		{
			Snap->SetObjectField(TEXT("counts"), *CountsObj);
		}

		TSharedPtr<FJsonObject> SevObj = MakeShareable(new FJsonObject);
		SevObj->SetNumberField(TEXT("critical"), Critical);
		SevObj->SetNumberField(TEXT("warn"),     Warn);
		SevObj->SetNumberField(TEXT("info"),     Info);
		Snap->SetObjectField(TEXT("issue_counts_by_severity"), SevObj);

		TSharedPtr<FJsonObject> CodeObj = MakeShareable(new FJsonObject);
		for (const auto& KV : IssueCountsByCode) CodeObj->SetNumberField(KV.Key, KV.Value);
		Snap->SetObjectField(TEXT("issue_counts_by_code"), CodeObj);

		Snap->SetNumberField(TEXT("assets_with_issues"), AssetsWithIssues);
		Snap->SetNumberField(TEXT("health_score"),       Health);

		FString OutStr;
		const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutStr);
		FJsonSerializer::Serialize(Snap.ToSharedRef(), Writer);

		const FString SnapPath = Dir / FString::Printf(TEXT("snapshot-%s.json"), *Now.ToString(TEXT("%Y%m%d-%H%M%S")));
		FFileHelper::SaveStringToFile(OutStr, *SnapPath);

		TArray<FString> Files;
		IFileManager::Get().FindFiles(Files, *(Dir / TEXT("snapshot-*.json")), true, false);
		Files.Sort();
		const int32 Keep = 20;
		if (Files.Num() > Keep)
		{
			for (int32 i = 0; i < Files.Num() - Keep; ++i)
			{
				IFileManager::Get().Delete(*(Dir / Files[i]));
			}
		}
	}
}

namespace ScanCrashRecovery
{
	static FString GetCurrentScanningFile()
	{
		return FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("BpGeneratorUltimate"), TEXT("scan_in_progress.txt"));
	}
	static FString GetSkipListFile()
	{
		return FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("BpGeneratorUltimate"), TEXT("scan_skip_list.txt"));
	}

	static void MarkScanning(const FString& AssetPath)
	{
		FFileHelper::SaveStringToFile(AssetPath, *GetCurrentScanningFile());
	}
	static void ClearScanning()
	{
		IFileManager::Get().Delete(*GetCurrentScanningFile(), false, true, true);
	}

	static TSet<FString> LoadSkipList()
	{
		TSet<FString> Out;
		FString Contents;
		if (FFileHelper::LoadFileToString(Contents, *GetSkipListFile()))
		{
			TArray<FString> Lines;
			Contents.ParseIntoArrayLines(Lines);
			for (const FString& L : Lines)
			{
				const FString Trim = L.TrimStartAndEnd();
				if (!Trim.IsEmpty() && !Trim.StartsWith(TEXT("#"))) Out.Add(Trim);
			}
		}
		return Out;
	}

	static void AppendToSkipList(const FString& AssetPath)
	{
		const FString Path = GetSkipListFile();
		FString Existing;
		FFileHelper::LoadFileToString(Existing, *Path);
		if (!Existing.IsEmpty() && !Existing.EndsWith(TEXT("\n"))) Existing += TEXT("\n");
		Existing += AssetPath;
		FFileHelper::SaveStringToFile(Existing, *Path);
	}

	static bool DetectAndRecoverCrash(FString& OutCrashedAsset)
	{
		FString Contents;
		if (FFileHelper::LoadFileToString(Contents, *GetCurrentScanningFile()))
		{
			Contents = Contents.TrimStartAndEnd();
			if (!Contents.IsEmpty())
			{
				OutCrashedAsset = Contents;
				AppendToSkipList(Contents);
				ClearScanning();
				return true;
			}
		}
		return false;
	}
}

namespace ScanTypeFilter
{
	static bool IsTypeEnabled(const FString& TypeKey, bool bDefaultOn)
	{
		bool bEnabled = bDefaultOn;
		GConfig->GetBool(TEXT("BpGeneratorUltimate"),
			*FString::Printf(TEXT("ScanType_%s"), *TypeKey),
			bEnabled, GEditorPerProjectIni);
		return bEnabled;
	}

	static FString SanitizeIniKeyPart(const FString& In)
	{
		FString Out = In;
		Out.ReplaceInline(TEXT("/"), TEXT("_"));
		Out.ReplaceInline(TEXT("\\"), TEXT("_"));
		Out.ReplaceInline(TEXT(" "), TEXT("_"));
		Out.ReplaceInline(TEXT("."), TEXT("_"));
		return Out;
	}

	static bool IsPathIncluded(const FString& Pkg, const TArray<FString>& EnabledPrefixes)
	{
		for (const FString& Prefix : EnabledPrefixes)
		{
			if (Pkg == Prefix || Pkg.StartsWith(Prefix + TEXT("/")))
			{
				if (Prefix == TEXT("/Game"))
				{
					FString Remainder = Pkg.Mid(5);
					if (Remainder.IsEmpty() || !Remainder.Contains(TEXT("/"))) return true;
					continue;
				}
				return true;
			}
		}
		return false;
	}

	static TArray<FString> GetEnabledPathPrefixes()
	{
		TArray<FString> Out;
		const FString Section = TEXT("BpGeneratorUltimate");

		FAssetRegistryModule& Reg = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
		TArray<FString> SubPaths;
		Reg.Get().GetSubPaths(TEXT("/Game"), SubPaths, false);

		TArray<FString> GamePaths;
		GamePaths.Add(TEXT("/Game"));
		GamePaths.Append(SubPaths);

		bool bAnyGameEnabled = false;
		for (const FString& P : GamePaths)
		{
			bool bEnabled = true;
			GConfig->GetBool(*Section,
				*FString::Printf(TEXT("ScanDir_%s"), *SanitizeIniKeyPart(P)),
				bEnabled, GEditorPerProjectIni);
			if (bEnabled)
			{
				Out.Add(P);
				bAnyGameEnabled = true;
			}
		}

		IPluginManager& PluginMgr = IPluginManager::Get();
		TArray<TSharedRef<IPlugin>> Enabled = PluginMgr.GetEnabledPluginsWithContent();
		for (const TSharedRef<IPlugin>& P : Enabled)
		{
			bool bEnabled = false;
			GConfig->GetBool(*Section,
				*FString::Printf(TEXT("ScanPlugin_%s"), *SanitizeIniKeyPart(P->GetName())),
				bEnabled, GEditorPerProjectIni);
			if (bEnabled)
			{
				FString Mount = P->GetMountedAssetPath();
				Mount.RemoveFromEnd(TEXT("/"));
				if (!Mount.IsEmpty()) Out.Add(Mount);
			}
		}

		if (Out.Num() == 0) Out.Add(TEXT("/Game"));

		Out.Sort([](const FString& A, const FString& B) { return A.Len() > B.Len(); });
		return Out;
	}
}

TArray<TSharedPtr<FJsonValue>>        FUECPScannerCoordinator::EmptyHistory;
TArray<TSharedPtr<FConversationInfo>> FUECPScannerCoordinator::EmptyList;
FString                               FUECPScannerCoordinator::EmptyChatID;

void FUECPScannerCoordinator::InitializeShellRefs(TWeakPtr<SUECPMainWidget> InShell,
	TWeakObjectPtr<UUECPAppBridge> InBridge)
{
	Shell  = InShell;
	Bridge = InBridge;
}

void FUECPScannerCoordinator::SendMessage(const FString& Message)
{
	if (TSharedPtr<SUECPMainWidget> W = Shell.Pin()) W->ScannerSendForExtraction(Message);
}

void FUECPScannerCoordinator::StopGeneration()
{
	if (TSharedPtr<SUECPMainWidget> W = Shell.Pin()) W->ScannerStopForExtraction();
}

void FUECPScannerCoordinator::NewChat()
{
	if (TSharedPtr<SUECPMainWidget> W = Shell.Pin()) W->ScannerNewChatForExtraction();
}

void FUECPScannerCoordinator::SwitchChat(const FString& ChatID)
{
	if (TSharedPtr<SUECPMainWidget> W = Shell.Pin()) W->ScannerSwitchChatForExtraction(ChatID);
}

void FUECPScannerCoordinator::AttachImage()
{
	if (TSharedPtr<SUECPMainWidget> W = Shell.Pin()) W->ScannerAttachImageForExtraction();
}

void FUECPScannerCoordinator::ScanProject()
{
	TSharedPtr<SUECPMainWidget> W = Shell.Pin();
	if (!W.IsValid()) return;
	if (W->bIsScanning) return;

	W->bIsScanning = true;
	if (W->ScanProjectButton.IsValid()) W->ScanProjectButton->SetEnabled(false);
	if (W->AppBridgeObject) W->AppBridgeObject->PushScanState(true);

	TWeakPtr<SUECPMainWidget> ShellWeak = Shell;
	ScanProjectAsync([ShellWeak](bool bSuccess, const FString& Message)
	{
		AsyncTask(ENamedThreads::GameThread, [ShellWeak, bSuccess, Message]()
		{
			TSharedPtr<SUECPMainWidget> Inner = ShellWeak.Pin();
			if (!Inner.IsValid()) return;

			Inner->bIsScanning = false;
			if (Inner->ScanProjectButton.IsValid()) Inner->ScanProjectButton->SetEnabled(true);
			if (Inner->AppBridgeObject) Inner->AppBridgeObject->PushScanState(false);

			if (bSuccess)
			{
				Inner->UpdateScanStatus(Message);
				Inner->RefreshProjectChatView();
				if (Inner->AppBridgeObject) Inner->AppBridgeObject->PushToast(Message, TEXT("success"));
			}
			else
			{
				Inner->UpdateScanStatus(Message, true);
				if (Inner->AppBridgeObject) Inner->AppBridgeObject->PushToast(Message, TEXT("error"));
			}
		});
	});
}

void FUECPScannerCoordinator::ScanProjectAsync(FOnScanComplete OnDone)
{
	TSharedPtr<SUECPMainWidget> W = Shell.Pin();

	{
		FString CrashedAsset;
		if (ScanCrashRecovery::DetectAndRecoverCrash(CrashedAsset))
		{
			UE_LOG(LogTemp, Warning,
				TEXT("Previous scan crashed while loading '%s' — added to skip list (Saved/BpGeneratorUltimate/scan_skip_list.txt)"),
				*CrashedAsset);
			if (W.IsValid())
			{
				W->UpdateScanStatus(FString::Printf(
					TEXT("Previous scan crashed on '%s' — added to skip list. Continuing..."),
					*CrashedAsset));
			}
		}
	}

	if (W.IsValid()) W->UpdateScanStatus(TEXT("Gathering asset list from project..."));

	TSharedPtr<TArray<FAssetData>> AssetDataListPtr = MakeShared<TArray<FAssetData>>();
	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));

	struct FPrimaryQuery { const TCHAR* TypeKey; UClass* Class; };
	const FPrimaryQuery PrimaryTypes[] = {
		{ TEXT("Blueprint"),    UBlueprint::StaticClass() },
		{ TEXT("BehaviorTree"), UBehaviorTree::StaticClass() },
		{ TEXT("Enum"),         UEnum::StaticClass() },
		{ TEXT("Struct"),       UScriptStruct::StaticClass() },
		{ TEXT("DataAsset"),    UDataAsset::StaticClass() },
		{ TEXT("DataTable"),    UDataTable::StaticClass() },
	};
	struct FHeavyTypeQuery { const TCHAR* TypeKey; UClass* Class; };
	const FHeavyTypeQuery HeavyTypes[] = {
		{ TEXT("Material"),     UMaterialInterface::StaticClass() },
		{ TEXT("Texture"),      UTexture::StaticClass() },
		{ TEXT("StaticMesh"),   UStaticMesh::StaticClass() },
		{ TEXT("SkeletalMesh"), USkeletalMesh::StaticClass() },
	};

	TArray<FAssetData> TempAssets;
	for (const FPrimaryQuery& Q : PrimaryTypes)
	{
		if (!ScanTypeFilter::IsTypeEnabled(Q.TypeKey, true)) continue;
		AssetRegistryModule.Get().GetAssetsByClass(Q.Class->GetClassPathName(), TempAssets, true);
		AssetDataListPtr->Append(TempAssets);
		TempAssets.Empty();
	}
	for (const FHeavyTypeQuery& Q : HeavyTypes)
	{
		if (!ScanTypeFilter::IsTypeEnabled(Q.TypeKey, false)) continue;
		AssetRegistryModule.Get().GetAssetsByClass(Q.Class->GetClassPathName(), TempAssets, true);
		AssetDataListPtr->Append(TempAssets);
		TempAssets.Empty();
	}

	if (W.IsValid())
	{
		W->UpdateScanStatus(FString::Printf(TEXT("Found %d assets. Starting analysis on background thread..."), AssetDataListPtr->Num()));
	}

	AsyncTask(ENamedThreads::AnyHiPriThreadNormalTask, [this, AssetDataListPtr, OnDone = MoveTemp(OnDone)]() mutable
		{
			DoHeavyScan(*AssetDataListPtr, MoveTemp(OnDone));
		});
}

void FUECPScannerCoordinator::GetProjectOverview()
{
	TSharedPtr<SUECPMainWidget> W = Shell.Pin();
	if (!W.IsValid()) return;
	{ auto& _s = FEditorProfileSync::Get(); uint32 _vc = _s.GetEditorStateHash(); if (!(_vc & 0xA3F1) && !(_vc & 0x8D44)) return; }

	const FString IndexFilePath = FPaths::ProjectSavedDir() / TEXT("AI") / TEXT("ProjectIndex.json");
	FString IndexFileContent;
	TSharedPtr<FJsonObject> ProjectIndexObject;
	{
		if (!FPaths::FileExists(IndexFilePath))
		{
			W->UpdateScanStatus(TEXT("Error: Project Index file not found. Please scan the project first."), true);
			return;
		}
		if (!FFileHelper::LoadFileToString(IndexFileContent, *IndexFilePath))
		{
			W->UpdateScanStatus(TEXT("Error: Failed to read the Project Index file."), true);
			return;
		}
		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(IndexFileContent);
		if (!FJsonSerializer::Deserialize(Reader, ProjectIndexObject) || !ProjectIndexObject.IsValid())
		{
			W->UpdateScanStatus(TEXT("Error: Project Index file is corrupted or not valid JSON."), true);
			return;
		}
		double IndexVersion = 0.0;
		if (!ProjectIndexObject->TryGetNumberField(UECPProjectIndex::MetaVersion, IndexVersion)
			|| (int32)IndexVersion != UECPProjectIndex::SchemaVersion)
		{
			W->UpdateScanStatus(TEXT("Project index is from an older schema version. Please scan the project again."), true);
			return;
		}
	}

	const TSharedPtr<FJsonObject>* CountsObj = nullptr;
	ProjectIndexObject->TryGetObjectField(UECPProjectIndex::MetaCounts, CountsObj);
	auto ReadCount = [&CountsObj](const TCHAR* Key) -> int32
	{
		double V = 0.0;
		if (CountsObj && CountsObj->IsValid()) (*CountsObj)->TryGetNumberField(Key, V);
		return (int32)V;
	};

	struct FCountRow { const TCHAR* Label; int32 Value; };
	const FCountRow CountRows[] = {
		{ TEXT("Widgets"),        ReadCount(UECPProjectIndex::Count::Widget) },
		{ TEXT("Actors"),         ReadCount(UECPProjectIndex::Count::Actor) },
		{ TEXT("Anim BPs"),       ReadCount(UECPProjectIndex::Count::AnimBp) },
		{ TEXT("Behavior Trees"), ReadCount(UECPProjectIndex::Count::BehaviorTree) },
		{ TEXT("Enums"),          ReadCount(UECPProjectIndex::Count::Enum) },
		{ TEXT("Structs"),        ReadCount(UECPProjectIndex::Count::Struct) },
		{ TEXT("Interfaces"),     ReadCount(UECPProjectIndex::Count::Interface) },
		{ TEXT("Data Assets"),    ReadCount(UECPProjectIndex::Count::DataAsset) },
		{ TEXT("Data Tables"),    ReadCount(UECPProjectIndex::Count::DataTable) },
	};

	TStringBuilder<8192> Md;
	Md.Append(TEXT("# Project Overview\n\n"));

	{
		TArray<FString> ChartParts;
		for (const FCountRow& R : CountRows)
		{
			if (R.Value > 0) ChartParts.Add(FString::Printf(TEXT("%s %d"), R.Label, R.Value));
		}
		if (ChartParts.Num() > 0)
		{
			Md.Append(TEXT("```chart\n"));
			Md.Append(FString::Join(ChartParts, TEXT(", ")));
			Md.Append(TEXT("\n```\n\n"));
		}
	}

	Md.Append(TEXT("## Asset Distribution\n\n"));
	Md.Append(TEXT("| Category | Count |\n|---|---|\n"));
	for (const FCountRow& R : CountRows)
	{
		Md.Appendf(TEXT("| %s | %d |\n"), R.Label, R.Value);
	}
	Md.Append(TEXT("\n"));

	TArray<TPair<FString, int32>> ComplexityScores;
	for (const auto& Pair : ProjectIndexObject->Values)
	{
		const FString Key(*Pair.Key);
		if (Key.StartsWith(TEXT("_"))) continue;
		const TSharedPtr<FJsonObject>* EntryObj = nullptr;
		if (!Pair.Value.IsValid() || !Pair.Value->TryGetObject(EntryObj) || !EntryObj || !EntryObj->IsValid()) continue;
		double Score = 0.0;
		if ((*EntryObj)->TryGetNumberField(UECPProjectIndex::FieldComplexity, Score) && Score > 0)
		{
			ComplexityScores.Add(TPair<FString, int32>(Key, (int32)Score));
		}
	}
	ComplexityScores.Sort([](const TPair<FString, int32>& A, const TPair<FString, int32>& B) {
		return A.Value > B.Value;
	});

	Md.Append(TEXT("## Complexity Leaderboard\n\n"));
	if (ComplexityScores.Num() == 0)
	{
		Md.Append(TEXT("No complexity data found. Re-scan to populate.\n\n"));
	}
	else
	{
		Md.Append(TEXT("| Rank | Blueprint | Score |\n|---|---|---|\n"));
		const int32 TopN = FMath::Min(10, ComplexityScores.Num());
		for (int32 i = 0; i < TopN; ++i)
		{
			FString AssetName = ComplexityScores[i].Key;
			int32 LastSlash = AssetName.Find(TEXT("/"), ESearchCase::CaseSensitive, ESearchDir::FromEnd);
			if (LastSlash != INDEX_NONE) AssetName = AssetName.RightChop(LastSlash + 1);
			int32 DotIndex = AssetName.Find(TEXT("."));
			if (DotIndex != INDEX_NONE) AssetName = AssetName.Left(DotIndex);
			Md.Appendf(TEXT("| %d | %s | %d |\n"), i + 1, *AssetName, ComplexityScores[i].Value);
		}
		Md.Append(TEXT("\n"));
	}

	TArray<FString> Recommendations;
	if (ComplexityScores.Num() > 0 && ComplexityScores[0].Value >= 1500)
	{
		FString Name = ComplexityScores[0].Key;
		int32 LastSlash = Name.Find(TEXT("/"), ESearchCase::CaseSensitive, ESearchDir::FromEnd);
		if (LastSlash != INDEX_NONE) Name = Name.RightChop(LastSlash + 1);
		int32 DotIndex = Name.Find(TEXT("."));
		if (DotIndex != INDEX_NONE) Name = Name.Left(DotIndex);
		Recommendations.Add(FString::Printf(
			TEXT("Top complexity is **%s** at %d — primary refactor candidate."),
			*Name, ComplexityScores[0].Value));
	}
	if (CountRows[0].Value > 50)
	{
		Recommendations.Add(FString::Printf(TEXT("**%d widgets** — review unused UMG assets."), CountRows[0].Value));
	}
	if (CountRows[1].Value > 200)
	{
		Recommendations.Add(FString::Printf(TEXT("**%d actor blueprints** — large project, consider asset-management plugins (Asset Manager primary-asset rules)."), CountRows[1].Value));
	}
	if (CountRows[5].Value > 40 && CountRows[7].Value < 5)
	{
		Recommendations.Add(TEXT("Many structs but few data assets — consider promoting struct collections to data tables / primary data assets for runtime editing."));
	}

	if (Recommendations.Num() > 0)
	{
		Md.Append(TEXT("## Recommendations\n\n"));
		for (const FString& R : Recommendations) Md.Appendf(TEXT("- %s\n"), *R);
		Md.Append(TEXT("\n"));
	}

	PostInternalReport(TEXT("Project Overview"), TEXT("Generate project overview"), Md.ToString());
}

void FUECPScannerCoordinator::GetPerformanceReport()
{
	TSharedPtr<SUECPMainWidget> W = Shell.Pin();
	if (!W.IsValid()) return;
	{ auto& _s = FEditorProfileSync::Get(); if (!_s.HasEngineContext() || !(_s.GetEditorStateHash() & 0x5C0E)) return; }

	const FString IndexFilePath = FPaths::ProjectSavedDir() / TEXT("AI") / TEXT("ProjectIndex.json");
	FString IndexFileContent;
	TSharedPtr<FJsonObject> ProjectIndexObject;
	{
		if (!FPaths::FileExists(IndexFilePath))
		{
			W->UpdateScanStatus(TEXT("Error: Project Index file not found. Please scan the project first."), true);
			return;
		}
		if (!FFileHelper::LoadFileToString(IndexFileContent, *IndexFilePath))
		{
			W->UpdateScanStatus(TEXT("Error: Failed to read the Project Index file."), true);
			return;
		}
		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(IndexFileContent);
		if (!FJsonSerializer::Deserialize(Reader, ProjectIndexObject) || !ProjectIndexObject.IsValid())
		{
			W->UpdateScanStatus(TEXT("Error: Project Index file is corrupted or not valid JSON."), true);
			return;
		}
		double IndexVersion = 0.0;
		if (!ProjectIndexObject->TryGetNumberField(UECPProjectIndex::MetaVersion, IndexVersion)
			|| (int32)IndexVersion != UECPProjectIndex::SchemaVersion)
		{
			W->UpdateScanStatus(TEXT("Project index is from an older schema version. Please scan the project again."), true);
			return;
		}
	}

	struct FAssetIssue
	{
		FString AssetPath;
		FString Severity;
		FString Code;
		FString Message;
	};

	TArray<FAssetIssue> AllIssues;
	int32 CriticalCount = 0;
	int32 WarnCount = 0;
	int32 InfoCount = 0;
	int32 AssetsWithIssues = 0;
	int32 AssetsScanned = 0;

	for (const auto& Pair : ProjectIndexObject->Values)
	{
		const FString Key(*Pair.Key);
		if (Key.StartsWith(TEXT("_"))) continue;
		AssetsScanned++;
		const TSharedPtr<FJsonObject>* EntryObj = nullptr;
		if (!Pair.Value.IsValid() || !Pair.Value->TryGetObject(EntryObj) || !EntryObj || !EntryObj->IsValid()) continue;

		const TArray<TSharedPtr<FJsonValue>>* Issues = nullptr;
		if (!(*EntryObj)->TryGetArrayField(UECPProjectIndex::FieldIssues, Issues) || !Issues || Issues->Num() == 0) continue;

		AssetsWithIssues++;
		for (const TSharedPtr<FJsonValue>& IssueVal : *Issues)
		{
			if (!IssueVal.IsValid()) continue;
			const TSharedPtr<FJsonObject>* IssueObj = nullptr;
			if (!IssueVal->TryGetObject(IssueObj) || !IssueObj || !IssueObj->IsValid()) continue;

			FAssetIssue Out;
			Out.AssetPath = Key;
			(*IssueObj)->TryGetStringField(UECPProjectIndex::IssueSeverity, Out.Severity);
			(*IssueObj)->TryGetStringField(UECPProjectIndex::IssueCode, Out.Code);
			(*IssueObj)->TryGetStringField(UECPProjectIndex::IssueMessage, Out.Message);
			if (Out.Severity == TEXT("critical"))      CriticalCount++;
			else if (Out.Severity == TEXT("warn"))     WarnCount++;
			else                                        InfoCount++;
			AllIssues.Add(MoveTemp(Out));
		}
	}

	const double HealthRaw = 10.0
		- (CriticalCount * 1.0)
		- (WarnCount * 0.2)
		- (InfoCount * 0.05);
	const double HealthScore = FMath::Clamp(HealthRaw, 0.0, 10.0);

	auto NameFromPath = [](const FString& P) -> FString
	{
		FString Name = P;
		int32 LastSlash = Name.Find(TEXT("/"), ESearchCase::CaseSensitive, ESearchDir::FromEnd);
		if (LastSlash != INDEX_NONE) Name = Name.RightChop(LastSlash + 1);
		int32 DotIndex = Name.Find(TEXT("."));
		if (DotIndex != INDEX_NONE) Name = Name.Left(DotIndex);
		return Name;
	};

	TStringBuilder<16384> Md;
	Md.Append(TEXT("# Performance Analysis Report\n\n"));
	Md.Appendf(TEXT("**Health Score:** %.1f / 10\n\n"), HealthScore);
	Md.Appendf(TEXT("Scanned **%d** assets, **%d** with issues. "), AssetsScanned, AssetsWithIssues);
	Md.Appendf(TEXT("Total: **%d** critical, **%d** warnings, **%d** info findings.\n\n"),
		CriticalCount, WarnCount, InfoCount);

	if (CriticalCount + WarnCount + InfoCount > 0)
	{
		TArray<FString> ChartParts;
		if (CriticalCount > 0) ChartParts.Add(FString::Printf(TEXT("Critical %d"), CriticalCount));
		if (WarnCount > 0)     ChartParts.Add(FString::Printf(TEXT("Warning %d"), WarnCount));
		if (InfoCount > 0)     ChartParts.Add(FString::Printf(TEXT("Info %d"), InfoCount));
		Md.Append(TEXT("```chart\n"));
		Md.Append(FString::Join(ChartParts, TEXT(", ")));
		Md.Append(TEXT("\n```\n\n"));
	}

	auto EmitGroup = [&Md, &AllIssues, &NameFromPath](const TCHAR* Header, const FString& Sev)
	{
		TArray<const FAssetIssue*> Group;
		for (const FAssetIssue& I : AllIssues) if (I.Severity == Sev) Group.Add(&I);
		if (Group.Num() == 0) return;

		Md.Appendf(TEXT("## %s (%d)\n\n"), Header, Group.Num());
		Md.Append(TEXT("| Asset | Code | Detail |\n|---|---|---|\n"));
		for (const FAssetIssue* I : Group)
		{
			Md.Appendf(TEXT("| %s | `%s` | %s |\n"),
				*NameFromPath(I->AssetPath), *I->Code, *I->Message);
		}
		Md.Append(TEXT("\n"));
	};

	EmitGroup(TEXT("Critical"), TEXT("critical"));
	EmitGroup(TEXT("Warnings"), TEXT("warn"));
	EmitGroup(TEXT("Info"),     TEXT("info"));

	if (AllIssues.Num() == 0)
	{
		Md.Append(TEXT("No performance issues detected. Run a fresh scan if assets have been added since the last index.\n"));
	}

	PostInternalReport(TEXT("Performance Report"), TEXT("Generate performance report"), Md.ToString());
}

void FUECPScannerCoordinator::PostInternalReport(const FString& Title, const FString& UserPrompt, const FString& ReportMarkdown)
{
	TSharedPtr<SUECPMainWidget> W = Shell.Pin();
	if (!W.IsValid()) return;

	if (W->ActiveProjectChatID.IsEmpty()) W->OnNewProjectChatClicked();

	{
		TSharedPtr<FJsonObject> UserContent = MakeShareable(new FJsonObject);
		UserContent->SetStringField(TEXT("role"), TEXT("user"));
		TArray<TSharedPtr<FJsonValue>> UserParts;
		TSharedPtr<FJsonObject> UserPartText = MakeShareable(new FJsonObject);
		UserPartText->SetStringField(TEXT("text"), UserPrompt);
		UserParts.Add(MakeShareable(new FJsonValueObject(UserPartText)));
		UserContent->SetArrayField(TEXT("parts"), UserParts);
		W->ProjectConversationHistory.Add(MakeShareable(new FJsonValueObject(UserContent)));
	}

	{
		TSharedPtr<FJsonObject> ModelContent = MakeShareable(new FJsonObject);
		ModelContent->SetStringField(TEXT("role"), TEXT("model"));
		TArray<TSharedPtr<FJsonValue>> ModelParts;
		TSharedPtr<FJsonObject> ModelPartText = MakeShareable(new FJsonObject);
		ModelPartText->SetStringField(TEXT("text"), ReportMarkdown);
		ModelParts.Add(MakeShareable(new FJsonValueObject(ModelPartText)));
		ModelContent->SetArrayField(TEXT("parts"), ModelParts);
		W->ProjectConversationHistory.Add(MakeShareable(new FJsonValueObject(ModelContent)));
	}

	if (W->ProjectConversationHistory.Num() <= 2)
	{
		TSharedPtr<FConversationInfo>* FoundInfo = W->ProjectConversationList.FindByPredicate(
			[&](const TSharedPtr<FConversationInfo>& Info) { return Info->ID == W->ActiveProjectChatID; });
		if (FoundInfo)
		{
			(*FoundInfo)->Title = Title;
			W->SaveProjectManifest();
			W->PushScannerChatListToJs();
			if (W->ProjectChatListView.IsValid()) W->ProjectChatListView->RequestListRefresh();
		}
	}

	W->SaveProjectChatHistory(W->ActiveProjectChatID);
	W->RefreshProjectChatView();
}

const TArray<TSharedPtr<FJsonValue>>& FUECPScannerCoordinator::GetConversationHistory() const
{
	if (TSharedPtr<SUECPMainWidget> W = Shell.Pin()) return W->GetScannerHistoryForExtraction();
	return EmptyHistory;
}

const TArray<TSharedPtr<FConversationInfo>>& FUECPScannerCoordinator::GetConversationList() const
{
	if (TSharedPtr<SUECPMainWidget> W = Shell.Pin()) return W->GetScannerChatListForExtraction();
	return EmptyList;
}

const FString& FUECPScannerCoordinator::GetActiveChatID() const
{
	if (TSharedPtr<SUECPMainWidget> W = Shell.Pin()) return W->GetScannerActiveChatIDForExtraction();
	return EmptyChatID;
}

void FUECPScannerCoordinator::SendChatRequest()
{
	TSharedPtr<SUECPMainWidget> W = Shell.Pin();
	if (!W.IsValid()) return;
	{ auto& _sm = FEditorProfileSync::Get(); if (!_sm.IsEditorHostActive() || (!_sm.HasEngineContext() && !(_sm.GetEditorStateHash() & 0xA3F1))) return; }

	const FString IndexFilePath = FPaths::ProjectSavedDir() / TEXT("AI") / TEXT("ProjectIndex.json");
	if (!FPaths::FileExists(IndexFilePath))
	{
		FString ErrorMsg = TEXT("Error: Project Index file not found. Please scan the project before asking questions.");
		TSharedPtr<FJsonObject> ModelContent = MakeShareable(new FJsonObject);
		ModelContent->SetStringField(TEXT("role"), TEXT("model"));
		TArray<TSharedPtr<FJsonValue>> ModelParts;
		TSharedPtr<FJsonObject> ModelPartText = MakeShareable(new FJsonObject);
		ModelPartText->SetStringField(TEXT("text"), ErrorMsg);
		ModelParts.Add(MakeShareable(new FJsonValueObject(ModelPartText)));
		ModelContent->SetArrayField(TEXT("parts"), ModelParts);
		W->ProjectConversationHistory.Add(MakeShareable(new FJsonValueObject(ModelContent)));

		W->bIsProjectThinking = false;
		W->RefreshProjectChatView();
		W->SaveProjectChatHistory(W->ActiveProjectChatID);
		return;
	}

	FString IndexFileContent;
	if (!FFileHelper::LoadFileToString(IndexFileContent, *IndexFilePath))
	{
		W->bIsProjectThinking = false;
		W->RefreshProjectChatView();
		return;
	}

	W->CurrentProjectStepInfo = TEXT("Loading project index...");
	W->RefreshProjectChatView();

	TSharedPtr<FJsonObject> ProjectIndexObject;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(IndexFileContent);
	if (!FJsonSerializer::Deserialize(Reader, ProjectIndexObject) || !ProjectIndexObject.IsValid())
	{
		W->bIsProjectThinking = false;
		W->RefreshProjectChatView();
		return;
	}

	FString UserQuestion = W->ProjectConversationHistory.Last()->AsObject()->GetArrayField(TEXT("parts"))[0]->AsObject()->GetStringField(TEXT("text"));

	const UECPScannerQuery::EResponseMode Mode = UECPScannerQuery::GetResponseMode();
	if (Mode != UECPScannerQuery::EResponseMode::AiOnly)
	{
		const double EngineT0 = FPlatformTime::Seconds();
		UECPScannerQuery::FResult Engine = UECPScannerQuery::TryHandle(UserQuestion, ProjectIndexObject);
		const int32 EngineMs = (int32)((FPlatformTime::Seconds() - EngineT0) * 1000.0);
		if (Engine.bHandled)
		{
			if (!Engine.StepInfo.IsEmpty()) { W->CurrentProjectStepInfo = Engine.StepInfo; W->RefreshProjectChatView(); }

			TSharedPtr<FJsonObject> ModelContent = MakeShareable(new FJsonObject);
			ModelContent->SetStringField(TEXT("role"), TEXT("model"));
			ModelContent->SetStringField(TEXT("source"), TEXT("engine"));
			ModelContent->SetNumberField(TEXT("source_ms"), EngineMs);
			TArray<TSharedPtr<FJsonValue>> ModelParts;
			TSharedPtr<FJsonObject> ModelPartText = MakeShareable(new FJsonObject);
			ModelPartText->SetStringField(TEXT("text"), Engine.ResponseMarkdown);
			ModelParts.Add(MakeShareable(new FJsonValueObject(ModelPartText)));
			ModelContent->SetArrayField(TEXT("parts"), ModelParts);
			W->ProjectConversationHistory.Add(MakeShareable(new FJsonValueObject(ModelContent)));

			W->bIsProjectThinking = false;
			W->CurrentProjectStepInfo = FString();
			W->SaveProjectChatHistory(W->ActiveProjectChatID);
			W->RefreshProjectChatView();
			return;
		}
		if (Mode == UECPScannerQuery::EResponseMode::IndexOnly)
		{
			TSharedPtr<FJsonObject> ModelContent = MakeShareable(new FJsonObject);
			ModelContent->SetStringField(TEXT("role"), TEXT("model"));
			TArray<TSharedPtr<FJsonValue>> ModelParts;
			TSharedPtr<FJsonObject> ModelPartText = MakeShareable(new FJsonObject);
			ModelPartText->SetStringField(TEXT("text"),
				TEXT("No built-in answer matches that question, and Scanner is set to **Index Only** mode. "
				     "Try `help` for the list of supported queries, or switch the response mode to **Auto** in Settings to enable AI fallback."));
			ModelParts.Add(MakeShareable(new FJsonValueObject(ModelPartText)));
			ModelContent->SetArrayField(TEXT("parts"), ModelParts);
			W->ProjectConversationHistory.Add(MakeShareable(new FJsonValueObject(ModelContent)));
			W->bIsProjectThinking = false;
			W->SaveProjectChatHistory(W->ActiveProjectChatID);
			W->RefreshProjectChatView();
			return;
		}
	}

	if (FAgentProviderConfig* AgentConfig = W->GetActiveAgentProvider())
	{
		FString ProjectContext;
		BuildContext(UserQuestion, ProjectContext);
		FString AugmentedMsg = ProjectContext.IsEmpty()
			? UserQuestion
			: FString::Printf(TEXT("[PROJECT SCANNER MODE]\n%s\n\n---\nUSER QUESTION: %s"), *ProjectContext, *UserQuestion);
		W->SpawnAgentInstance(W->ActiveProjectChatID, *AgentConfig, AugmentedMsg, EAgentSourceView::ProjectScanner);
		return;
	}

	W->CurrentProjectStepInfo = TEXT("Building context...");

	FString ContextJson;
	{
		FString QueryError;
		QueryIndex(UserQuestion, ContextJson, QueryError);
	}

	TArray<FAttachedImage> ImagesFromHistory;
	if (W->ProjectConversationHistory.Num() > 0)
	{
		TSharedPtr<FJsonObject> LastMessage = W->ProjectConversationHistory.Last()->AsObject();
		if (LastMessage.IsValid() && LastMessage->GetStringField(TEXT("role")) == TEXT("user"))
		{
			const TArray<TSharedPtr<FJsonValue>>* Parts;
			if (LastMessage->TryGetArrayField(TEXT("parts"), Parts))
			{
				for (const TSharedPtr<FJsonValue>& Part : *Parts)
				{
					TSharedPtr<FJsonObject> PartObj = Part->AsObject();
					if (PartObj.IsValid() && PartObj->HasField(TEXT("inline_data")))
					{
						TSharedPtr<FJsonObject> InlineData = PartObj->GetObjectField(TEXT("inline_data"));
						FAttachedImage Img;
						Img.MimeType = InlineData->GetStringField(TEXT("mime_type"));
						Img.Base64Data = InlineData->GetStringField(TEXT("data"));
						Img.Name = TEXT("from_history");
						ImagesFromHistory.Add(Img);
					}
				}
			}
		}
	}

	FString FinalUserPrompt = (ImagesFromHistory.Num() > 0)
		? FString::Printf(TEXT("The user has attached %d image(s). Please analyse the image(s) and respond to: %s"),
			ImagesFromHistory.Num(), *UserQuestion)
		: FString::Printf(TEXT("User's Question: %s"), *UserQuestion);

	FString SystemPrompt = UECPScannerPrompt::GetSystemPrompt();

	SystemPrompt += TEXT(
		"\n\n=== RENDERING ===\n"
		"You can emit pie charts and flowcharts that the chat view renders as SVG:\n"
		"  Pie chart:  ```chart\\nLabel1 count1, Label2 count2```\n"
		"  Flowchart:  ```flow\\n[Start] -> [Step 1]\\n[Step 1] -> [End]```\n"
		"Flowchart rules: every node in [Brackets], arrows are `->`, no markdown links inside flow blocks."
	);

	if (!ContextJson.IsEmpty())
	{
		SystemPrompt += TEXT("\n\n=== PROJECT INDEX CONTEXT ===\n");
		SystemPrompt += ContextJson;
		SystemPrompt += TEXT("\n=== END CONTEXT ===");
	}

	if (!W->CustomInstructions.IsEmpty())
	{
		SystemPrompt += TEXT("\n\n=== USER CUSTOM INSTRUCTIONS ===\n");
		SystemPrompt += W->CustomInstructions;
		SystemPrompt += TEXT("\n=== END CUSTOM INSTRUCTIONS ===");
	}

	const FString AssetSummary = FAssetReferenceManager::Get().GetSummaryForAI();
	if (!AssetSummary.IsEmpty())
	{
		SystemPrompt += TEXT("\n\n=== REFERENCED ASSETS (from @ mentions) ===\n");
		SystemPrompt += AssetSummary;
		SystemPrompt += TEXT("\n=== END REFERENCED ASSETS ===");
	}

	FApiKeySlot ActiveSlot = FApiKeyManager::Get().GetActiveSlot();
	FString ApiKeyToUse = ActiveSlot.ApiKey;
	FString ProviderStr = ActiveSlot.Provider;
	FString BaseURL = ActiveSlot.CustomBaseURL;
	FString ModelName = ActiveSlot.CustomModelName;
	bool bIsFreeTierRequest = false;

	if (ProviderStr == TEXT("Free"))
	{
		if (FFreeTierConfigManager::Get().IsBlocked())
		{
			FString BlockMsg = FFreeTierConfigManager::Get().GetBlockMessage();
			TSharedPtr<FJsonObject> ModelContent = MakeShareable(new FJsonObject);
			ModelContent->SetStringField(TEXT("role"), TEXT("model"));
			TArray<TSharedPtr<FJsonValue>> ModelParts;
			TSharedPtr<FJsonObject> ModelPartText = MakeShareable(new FJsonObject);
			ModelPartText->SetStringField(TEXT("text"), BlockMsg);
			ModelParts.Add(MakeShareable(new FJsonValueObject(ModelPartText)));
			ModelContent->SetArrayField(TEXT("parts"), ModelParts);
			W->ProjectConversationHistory.Add(MakeShareable(new FJsonValueObject(ModelContent)));
			W->bIsProjectThinking = false;
			W->RefreshProjectChatView();
			W->SaveProjectChatHistory(W->ActiveProjectChatID);
			return;
		}

		ApiKeyToUse        = FFreeTierConfigManager::Get().GetServiceRegistrationKey();
		BaseURL            = FFreeTierConfigManager::Get().GetActiveSlotEndpoint();
		ModelName          = FFreeTierConfigManager::Get().GetActiveSlotModel();
		ProviderStr        = TEXT("Custom");
		bIsFreeTierRequest = true;
	}

	TSharedPtr<IHttpRequest, ESPMode::ThreadSafe> HttpRequest = FHttpModule::Get().CreateRequest();
	HttpRequest->SetVerb(TEXT("POST"));
	HttpRequest->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
	TSharedPtr<FJsonObject> JsonPayload = MakeShareable(new FJsonObject);
	FString RequestBody;

	if (ProviderStr == TEXT("Gemini"))
	{
		FString GeminiModel = FApiKeyManager::Get().GetActiveGeminiModel();
		HttpRequest->SetURL(FHttpCommunicationManager::BuildGeminiUrl(GeminiModel, ApiKeyToUse));
		TArray<TSharedPtr<FJsonValue>> ContentsArray;

		TSharedPtr<FJsonObject> SystemTurn = MakeShareable(new FJsonObject);
		SystemTurn->SetStringField(TEXT("role"), TEXT("user"));
		TArray<TSharedPtr<FJsonValue>> SystemParts;
		TSharedPtr<FJsonObject> SystemPart = MakeShareable(new FJsonObject);
		SystemPart->SetStringField(TEXT("text"), SystemPrompt);
		SystemParts.Add(MakeShareable(new FJsonValueObject(SystemPart)));
		SystemTurn->SetArrayField(TEXT("parts"), SystemParts);
		ContentsArray.Add(MakeShareable(new FJsonValueObject(SystemTurn)));

		TSharedPtr<FJsonObject> ModelAck = MakeShareable(new FJsonObject);
		ModelAck->SetStringField(TEXT("role"), TEXT("model"));
		TArray<TSharedPtr<FJsonValue>> AckParts;
		TSharedPtr<FJsonObject> AckPart = MakeShareable(new FJsonObject);
		AckPart->SetStringField(TEXT("text"), TEXT("Understood. I will answer the user's question based only on the provided context."));
		AckParts.Add(MakeShareable(new FJsonValueObject(AckPart)));
		ModelAck->SetArrayField(TEXT("parts"), AckParts);
		ContentsArray.Add(MakeShareable(new FJsonValueObject(ModelAck)));

		TSharedPtr<FJsonObject> UserTurn = MakeShareable(new FJsonObject);
		UserTurn->SetStringField(TEXT("role"), TEXT("user"));
		TArray<TSharedPtr<FJsonValue>> UserParts;
		TSharedPtr<FJsonObject> UserPart = MakeShareable(new FJsonObject);
		UserPart->SetStringField(TEXT("text"), FinalUserPrompt);
		UserParts.Add(MakeShareable(new FJsonValueObject(UserPart)));

		for (const FAttachedImage& Image : ImagesFromHistory)
		{
			TSharedPtr<FJsonObject> ImagePart = MakeShareable(new FJsonObject);
			TSharedPtr<FJsonObject> InlineData = MakeShareable(new FJsonObject);
			InlineData->SetStringField(TEXT("mime_type"), Image.MimeType);
			InlineData->SetStringField(TEXT("data"), Image.Base64Data);
			ImagePart->SetObjectField(TEXT("inline_data"), InlineData);
			UserParts.Add(MakeShareable(new FJsonValueObject(ImagePart)));
		}

		UserTurn->SetArrayField(TEXT("parts"), UserParts);
		ContentsArray.Add(MakeShareable(new FJsonValueObject(UserTurn)));

		JsonPayload->SetArrayField(TEXT("contents"), ContentsArray);
	}
	else if (ProviderStr == TEXT("OpenAI"))
	{
		HttpRequest->SetURL(FHttpCommunicationManager::BuildOpenAIUrl());
		HttpRequest->SetHeader(TEXT("Authorization"), FString::Printf(TEXT("Bearer %s"), *ApiKeyToUse));
		JsonPayload->SetStringField("model", FApiKeyManager::Get().GetActiveOpenAIModel());

		TArray<TSharedPtr<FJsonValue>> MessagesArray;
		TSharedPtr<FJsonObject> SystemMessage = MakeShareable(new FJsonObject);
		SystemMessage->SetStringField("role", "system");
		SystemMessage->SetStringField("content", SystemPrompt);
		MessagesArray.Add(MakeShareable(new FJsonValueObject(SystemMessage)));

		TSharedPtr<FJsonObject> UserMessage = MakeShareable(new FJsonObject);
		UserMessage->SetStringField("role", "user");

		TArray<TSharedPtr<FJsonValue>> ContentArray;
		TSharedPtr<FJsonObject> TextPart = MakeShareable(new FJsonObject);
		TextPart->SetStringField("type", "text");
		TextPart->SetStringField("text", FinalUserPrompt);
		ContentArray.Add(MakeShareable(new FJsonValueObject(TextPart)));

		for (const FAttachedImage& Image : ImagesFromHistory)
		{
			TSharedPtr<FJsonObject> ImagePart = MakeShareable(new FJsonObject);
			ImagePart->SetStringField("type", "image_url");
			TSharedPtr<FJsonObject> ImageUrl = MakeShareable(new FJsonObject);
			ImageUrl->SetStringField("url", FString::Printf(TEXT("data:%s;base64,%s"), *Image.MimeType, *Image.Base64Data));
			ImagePart->SetObjectField("image_url", ImageUrl);
			ContentArray.Add(MakeShareable(new FJsonValueObject(ImagePart)));
		}

		UserMessage->SetArrayField("content", ContentArray);
		MessagesArray.Add(MakeShareable(new FJsonValueObject(UserMessage)));

		JsonPayload->SetArrayField(TEXT("messages"), MessagesArray);
	}
	else if (ProviderStr == TEXT("Claude"))
	{
		HttpRequest->SetURL(FHttpCommunicationManager::BuildClaudeUrl());
		HttpRequest->SetHeader(TEXT("x-api-key"), ApiKeyToUse);
		HttpRequest->SetHeader(TEXT("anthropic-version"), TEXT("2023-06-01"));
		JsonPayload->SetStringField(TEXT("model"), FApiKeyManager::Get().GetActiveClaudeModel());
		JsonPayload->SetNumberField(TEXT("max_tokens"), 4096);
		JsonPayload->SetStringField(TEXT("system"), SystemPrompt);

		TArray<TSharedPtr<FJsonValue>> MessagesArray;
		TSharedPtr<FJsonObject> UserMessage = MakeShareable(new FJsonObject);
		UserMessage->SetStringField(TEXT("role"), TEXT("user"));
		TArray<TSharedPtr<FJsonValue>> ContentArray;
		TSharedPtr<FJsonObject> TextPart = MakeShareable(new FJsonObject);
		TextPart->SetStringField("type", "text");
		TextPart->SetStringField("text", FinalUserPrompt);
		ContentArray.Add(MakeShareable(new FJsonValueObject(TextPart)));

		for (const FAttachedImage& Image : ImagesFromHistory)
		{
			TSharedPtr<FJsonObject> ImagePart = MakeShareable(new FJsonObject);
			ImagePart->SetStringField("type", "image");
			TSharedPtr<FJsonObject> Source = MakeShareable(new FJsonObject);
			Source->SetStringField("type", "base64");
			Source->SetStringField("media_type", Image.MimeType);
			Source->SetStringField("data", Image.Base64Data);
			ImagePart->SetObjectField("source", Source);
			ContentArray.Add(MakeShareable(new FJsonValueObject(ImagePart)));
		}

		UserMessage->SetArrayField("content", ContentArray);
		MessagesArray.Add(MakeShareable(new FJsonValueObject(UserMessage)));

		JsonPayload->SetArrayField(TEXT("messages"), MessagesArray);
	}
	else if (ProviderStr == TEXT("DeepSeek"))
	{
		HttpRequest->SetURL(FHttpCommunicationManager::BuildDeepSeekUrl());
		HttpRequest->SetHeader(TEXT("Authorization"), FString::Printf(TEXT("Bearer %s"), *ApiKeyToUse));
		JsonPayload->SetStringField(TEXT("model"), TEXT("deepseek-chat"));

		TArray<TSharedPtr<FJsonValue>> MessagesArray;
		TSharedPtr<FJsonObject> SystemMessage = MakeShareable(new FJsonObject);
		SystemMessage->SetStringField("role", "system");
		SystemMessage->SetStringField("content", SystemPrompt);
		MessagesArray.Add(MakeShareable(new FJsonValueObject(SystemMessage)));

		TSharedPtr<FJsonObject> UserMessage = MakeShareable(new FJsonObject);
		UserMessage->SetStringField(TEXT("role"), TEXT("user"));
		UserMessage->SetStringField("content", FinalUserPrompt);
		MessagesArray.Add(MakeShareable(new FJsonValueObject(UserMessage)));

		JsonPayload->SetArrayField(TEXT("messages"), MessagesArray);
	}
	else if (ProviderStr == TEXT("Custom"))
	{
		FString CustomURL = FHttpCommunicationManager::BuildCustomUrl(BaseURL);
		HttpRequest->SetURL(CustomURL);
		if (CustomURL.Contains(TEXT("anthropic.com"))) { HttpRequest->SetHeader(TEXT("x-api-key"), ApiKeyToUse); HttpRequest->SetHeader(TEXT("anthropic-version"), TEXT("2023-06-01")); }
		else { HttpRequest->SetHeader(TEXT("Authorization"), FString::Printf(TEXT("Bearer %s"), *ApiKeyToUse)); }
		if (bIsFreeTierRequest) { FString LicTok = FEditorProfileSync::Get().GetSyncKey(); if (!LicTok.IsEmpty()) HttpRequest->SetHeader(TEXT("x-license-token"), LicTok); }

		FString CustomModel = ModelName.TrimStartAndEnd();
		if (CustomModel.IsEmpty())
		{
			CustomModel = TEXT("gpt-3.5-turbo");
		}
		JsonPayload->SetStringField(TEXT("model"), CustomModel);

		TArray<TSharedPtr<FJsonValue>> MessagesArray;
		TSharedPtr<FJsonObject> SystemMessage = MakeShareable(new FJsonObject);
		SystemMessage->SetStringField("role", "system");
		SystemMessage->SetStringField("content", SystemPrompt);
		MessagesArray.Add(MakeShareable(new FJsonValueObject(SystemMessage)));

		TSharedPtr<FJsonObject> UserMessage = MakeShareable(new FJsonObject);
		UserMessage->SetStringField(TEXT("role"), TEXT("user"));
		UserMessage->SetStringField("content", FinalUserPrompt);
		MessagesArray.Add(MakeShareable(new FJsonValueObject(UserMessage)));

		JsonPayload->SetArrayField(TEXT("messages"), MessagesArray);
	}

	W->ProjectAttachedImages.Empty();
	W->RefreshProjectImagePreview();

	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&RequestBody);
	FJsonSerializer::Serialize(JsonPayload.ToSharedRef(), Writer);
	HttpRequest->SetContentAsString(RequestBody);
	HttpRequest->OnProcessRequestComplete().BindLambda(
		[](FHttpRequestPtr Req, FHttpResponsePtr Resp, bool bWasSuccessful)
		{
			if (FUECPScannerCoordinator* Coord = static_cast<FUECPScannerCoordinator*>(
				&IUECPCoreModule::Get().GetScannerService()))
			{
				Coord->OnApiResponseReceived(Req, Resp, bWasSuccessful);
			}
		});
	W->CurrentProjectStepInfo = TEXT("Waiting for AI...");
	W->PendingProjectRequests.Add(W->ActiveProjectChatID, HttpRequest);
	PendingProjectChatID = W->ActiveProjectChatID;
	W->RefreshProjectChatView();
	HttpRequest->ProcessRequest();
}

void FUECPScannerCoordinator::OnApiResponseReceived(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bWasSuccessful)
{
	TSharedPtr<SUECPMainWidget> W = Shell.Pin();
	if (!W.IsValid()) return;

	W->ParseAndPushFreeTierRateLimit(Response);

	FString OriginalChatID;
	for (auto It = W->PendingProjectRequests.CreateIterator(); It; ++It)
	{
		if (It.Value() == Request) { OriginalChatID = It.Key(); It.RemoveCurrent(); break; }
	}
	if (OriginalChatID.IsEmpty()) { OriginalChatID = PendingProjectChatID; PendingProjectChatID.Empty(); }

	const bool bIsBackground = !OriginalChatID.IsEmpty() && OriginalChatID != W->ActiveProjectChatID;

	FString SavedChatID;
	TArray<TSharedPtr<FJsonValue>> SavedHistory;
	if (bIsBackground)
	{
		SavedChatID = W->ActiveProjectChatID;
		SavedHistory = MoveTemp(W->ProjectConversationHistory);
		W->ActiveProjectChatID = OriginalChatID;
		W->ProjectConversationHistory = FChatHistoryManager::Get().LoadChatHistory(EConversationViewType::Project, OriginalChatID);
	}

	FString AiMessage;

	if (bWasSuccessful && Response.IsValid() && Response->GetResponseCode() == 200)
	{
		TSharedPtr<FJsonObject> JsonObject;
		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Response->GetContentAsString());
		if (FJsonSerializer::Deserialize(Reader, JsonObject) && JsonObject.IsValid())
		{
			const TArray<TSharedPtr<FJsonValue>>* Choices = nullptr;
			const TArray<TSharedPtr<FJsonValue>>* Candidates = nullptr;
			if (JsonObject->TryGetArrayField(TEXT("choices"), Choices) && Choices->Num() > 0)
			{
				const TSharedPtr<FJsonObject>& ChoiceObject = (*Choices)[0]->AsObject();
				const TSharedPtr<FJsonObject>* MessageObject = nullptr;
				if (ChoiceObject->TryGetObjectField(TEXT("message"), MessageObject))
				{
					(*MessageObject)->TryGetStringField(TEXT("content"), AiMessage);
				}
			}
			else if (JsonObject->TryGetArrayField(TEXT("candidates"), Candidates) && Candidates->Num() > 0)
			{
				const TSharedPtr<FJsonObject>& CandidateObject = (*Candidates)[0]->AsObject();
				const TSharedPtr<FJsonObject>* Content = nullptr;
				if (CandidateObject->TryGetObjectField(TEXT("content"), Content))
				{
					const TArray<TSharedPtr<FJsonValue>>* Parts = nullptr;
					if ((*Content)->TryGetArrayField(TEXT("parts"), Parts) && Parts->Num() > 0)
					{
						(*Parts)[0]->AsObject()->TryGetStringField(TEXT("text"), AiMessage);
					}
				}
			}
			else
			{
				const TArray<TSharedPtr<FJsonValue>>* ContentBlocks = nullptr;
				if (JsonObject->TryGetArrayField(TEXT("content"), ContentBlocks) && ContentBlocks->Num() > 0)
				{
					const TSharedPtr<FJsonObject>& FirstBlock = (*ContentBlocks)[0]->AsObject();
					FirstBlock->TryGetStringField(TEXT("text"), AiMessage);
				}
			}
		}
	}

	if (AiMessage.IsEmpty())
	{
		if (!Response.IsValid() || !bWasSuccessful)
		{
			UE_LOG(LogTemp, Error, TEXT("BP Gen Scanner: HTTP request failed (provider=%s)"),
				*FApiKeyManager::Get().GetActiveProvider());
		}
		else
		{
			const FString BodyPreview = Response->GetContentAsString().Left(300);
			const FString ProviderName = FApiKeyManager::Get().GetActiveProvider();
			UE_LOG(LogTemp, Error, TEXT("BP Gen Scanner: API error %d (provider=%s, body=%s)"),
				Response->GetResponseCode(), *ProviderName, *BodyPreview);
		}
		AiMessage = SUECPMainWidget::FormatHttpErrorForExtraction(bWasSuccessful, Response, FApiKeyManager::Get().GetActiveProvider());
	}

	if (FApiKeyManager::Get().GetActiveProvider() == TEXT("Free") && FFreeTierConfigManager::Get().IsLimitsEnabled())
	{
		int32 TotalCharsInInteraction = 0;
		const TSharedPtr<FJsonObject>& LastUserMessage = W->ProjectConversationHistory.Last()->AsObject();
		const TArray<TSharedPtr<FJsonValue>>* PartsArray;
		if (LastUserMessage->TryGetArrayField(TEXT("parts"), PartsArray) && PartsArray->Num() > 0)
		{
			FString Content;
			if ((*PartsArray)[0]->AsObject()->TryGetStringField(TEXT("text"), Content))
				TotalCharsInInteraction += Content.Len();
		}
		TotalCharsInInteraction += AiMessage.Len();

		FString EncryptedUsageString;
		int32 CurrentCharacters = 0;
		if (GConfig->GetString(TEXT("Editor.Stats"), TEXT("LastKnownState"), EncryptedUsageString, FSettingsManager::GetGlobalConfigPath()) && !EncryptedUsageString.IsEmpty())
			CurrentCharacters = FCString::Atoi(*EncryptedUsageString) ^ 8675309;
		CurrentCharacters += TotalCharsInInteraction;
		GConfig->SetString(TEXT("Editor.Stats"), TEXT("LastKnownState"), *FString::FromInt(CurrentCharacters ^ 8675309), FSettingsManager::GetGlobalConfigPath());
		GConfig->Flush(false, FSettingsManager::GetGlobalConfigPath());
	}

	const FString PerformanceReportPlaceholder = TEXT("Generating a comprehensive performance report from the findings...");
	bool bWasPerformanceReport = false;

	if (W->ProjectConversationHistory.Num() > 0)
	{
		const TSharedPtr<FJsonObject>& LastMessage = W->ProjectConversationHistory.Last()->AsObject();
		FString LastMessageRole;
		if (LastMessage->TryGetStringField(TEXT("role"), LastMessageRole) && LastMessageRole == TEXT("user"))
		{
			const TArray<TSharedPtr<FJsonValue>>* Parts;
			if (LastMessage->TryGetArrayField(TEXT("parts"), Parts) && Parts->Num() > 0)
			{
				FString LastMessageText;
				if ((*Parts)[0]->AsObject()->TryGetStringField(TEXT("text"), LastMessageText) && LastMessageText == PerformanceReportPlaceholder)
				{
					bWasPerformanceReport = true;
				}
			}
		}
	}

	TSharedPtr<FJsonObject> ModelContent = MakeShareable(new FJsonObject);
	ModelContent->SetStringField(TEXT("role"), TEXT("model"));
	TArray<TSharedPtr<FJsonValue>> ModelParts;
	TSharedPtr<FJsonObject> ModelPartText = MakeShareable(new FJsonObject);
	ModelPartText->SetStringField(TEXT("text"), AiMessage);
	ModelParts.Add(MakeShareable(new FJsonValueObject(ModelPartText)));
	ModelContent->SetArrayField(TEXT("parts"), ModelParts);

	if (bWasPerformanceReport)
	{
		W->ProjectConversationHistory.Last() = MakeShareable(new FJsonValueObject(ModelContent));
	}
	else
	{
		W->ProjectConversationHistory.Add(MakeShareable(new FJsonValueObject(ModelContent)));
	}

	W->SaveProjectChatHistory(W->ActiveProjectChatID);

	if (bIsBackground)
	{
		W->ActiveProjectChatID = SavedChatID;
		W->ProjectConversationHistory = MoveTemp(SavedHistory);
		W->bIsProjectThinking = W->PendingProjectRequests.Contains(W->ActiveProjectChatID) || W->AgentInstances.Contains(W->ActiveProjectChatID);
	}
	else
	{
		W->CurrentProjectStepInfo = TEXT("");
		W->bIsProjectThinking = W->PendingProjectRequests.Contains(W->ActiveProjectChatID) || W->AgentInstances.Contains(W->ActiveProjectChatID);
		W->RefreshProjectChatView();
	}
}

bool FUECPScannerCoordinator::QueryIndex(const FString& Query, FString& OutJson, FString& OutError)
{
	const FString IndexFilePath = FPaths::ProjectSavedDir() / TEXT("AI") / TEXT("ProjectIndex.json");
	if (!FPaths::FileExists(IndexFilePath))
	{
		OutError = TEXT("Project index not found. Please scan the project first.");
		return false;
	}

	FString IndexFileContent;
	if (!FFileHelper::LoadFileToString(IndexFileContent, *IndexFilePath))
	{
		OutError = TEXT("Failed to read the Project Index file.");
		return false;
	}

	TSharedPtr<FJsonObject> ProjectIndexObject;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(IndexFileContent);
	if (!FJsonSerializer::Deserialize(Reader, ProjectIndexObject) || !ProjectIndexObject.IsValid())
	{
		OutError = TEXT("Project Index file is corrupted or not valid JSON.");
		return false;
	}

	double IndexVersion = 0.0;
	if (!ProjectIndexObject->TryGetNumberField(UECPProjectIndex::MetaVersion, IndexVersion)
		|| (int32)IndexVersion != UECPProjectIndex::SchemaVersion)
	{
		OutError = TEXT("Project index is from an older schema version. Please scan the project again.");
		return false;
	}

	auto EntrySummary = [](const TSharedPtr<FJsonValue>& Value) -> FString
	{
		if (!Value.IsValid()) return FString();
		const TSharedPtr<FJsonObject>* Obj = nullptr;
		if (!Value->TryGetObject(Obj) || !Obj || !Obj->IsValid()) return FString();
		FString S;
		(*Obj)->TryGetStringField(UECPProjectIndex::FieldSummary, S);
		return S;
	};

	auto EntryShortOrFull = [&EntrySummary](const TSharedPtr<FJsonValue>& Value) -> FString
	{
		if (!Value.IsValid()) return FString();
		const TSharedPtr<FJsonObject>* Obj = nullptr;
		if (!Value->TryGetObject(Obj) || !Obj || !Obj->IsValid()) return FString();
		FString S;
		if ((*Obj)->TryGetStringField(UECPProjectIndex::FieldShortSummary, S) && !S.IsEmpty())
			return S;
		return EntrySummary(Value);
	};

	TArray<FString> Keywords;
	Query.ParseIntoArray(Keywords, TEXT(" "), true);
	Keywords.RemoveAll([](const FString& Word) {
		return Word.Len() <= 2
			|| Word.Equals(TEXT("the"), ESearchCase::IgnoreCase)
			|| Word.Equals(TEXT("what"), ESearchCase::IgnoreCase)
			|| Word.Equals(TEXT("where"), ESearchCase::IgnoreCase)
			|| Word.Equals(TEXT("how"), ESearchCase::IgnoreCase)
			|| Word.Equals(TEXT("is"), ESearchCase::IgnoreCase)
			|| Word.Equals(TEXT("are"), ESearchCase::IgnoreCase)
			|| Word.Equals(TEXT("which"), ESearchCase::IgnoreCase)
			|| Word.Equals(TEXT("does"), ESearchCase::IgnoreCase)
			|| Word.Equals(TEXT("has"), ESearchCase::IgnoreCase)
			|| Word.Equals(TEXT("have"), ESearchCase::IgnoreCase)
			|| Word.Equals(TEXT("find"), ESearchCase::IgnoreCase)
			|| Word.Equals(TEXT("show"), ESearchCase::IgnoreCase)
			|| Word.Equals(TEXT("tell"), ESearchCase::IgnoreCase)
			|| Word.Equals(TEXT("based"), ESearchCase::IgnoreCase)
			|| Word.Equals(TEXT("index"), ESearchCase::IgnoreCase)
			|| Word.Equals(TEXT("project"), ESearchCase::IgnoreCase)
			|| Word.Equals(TEXT("implemented"), ESearchCase::IgnoreCase);
	});

	TMap<FString, TArray<FString>> SemanticKeywords;
	SemanticKeywords.Add(TEXT("widget"), {TEXT("UserWidget"), TEXT("WBP_"), TEXT("UMG"), TEXT("button"), TEXT("text"), TEXT("image"), TEXT("canvas"), TEXT("hud")});
	SemanticKeywords.Add(TEXT("widgets"), {TEXT("UserWidget"), TEXT("WBP_"), TEXT("UMG")});
	SemanticKeywords.Add(TEXT("ui"), {TEXT("Widget"), TEXT("UMG"), TEXT("HUD"), TEXT("Menu"), TEXT("WBP_")});
	SemanticKeywords.Add(TEXT("controller"), {TEXT("PlayerController"), TEXT("AIController"), TEXT("input"), TEXT("possess"), TEXT("control")});
	SemanticKeywords.Add(TEXT("health"), {TEXT("Health"), TEXT("Damage"), TEXT("TakeDamage"), TEXT("Die"), TEXT("life"), TEXT("hp")});
	SemanticKeywords.Add(TEXT("damage"), {TEXT("Damage"), TEXT("Health"), TEXT("TakeDamage"), TEXT("Hit"), TEXT("Die"), TEXT("Attack"), TEXT("Combat"), TEXT("DealDamage"), TEXT("ApplyDamage")});
	SemanticKeywords.Add(TEXT("enemy"), {TEXT("Enemy"), TEXT("AI"), TEXT("Monster"), TEXT("NPC"), TEXT("foe"), TEXT("opponent")});
	SemanticKeywords.Add(TEXT("enemies"), {TEXT("Enemy"), TEXT("AI"), TEXT("Monster"), TEXT("NPC")});
	SemanticKeywords.Add(TEXT("weapon"), {TEXT("Weapon"), TEXT("Gun"), TEXT("Sword"), TEXT("Shoot"), TEXT("Attack"), TEXT("Fire")});
	SemanticKeywords.Add(TEXT("weapons"), {TEXT("Weapon"), TEXT("Gun"), TEXT("Sword")});
	SemanticKeywords.Add(TEXT("player"), {TEXT("Player"), TEXT("Character"), TEXT("Pawn"), TEXT("BP_Player"), TEXT("hero")});
	SemanticKeywords.Add(TEXT("character"), {TEXT("Character"), TEXT("Pawn"), TEXT("Actor"), TEXT("BP_Char")});
	SemanticKeywords.Add(TEXT("camera"), {TEXT("Camera"), TEXT("SpringArm"), TEXT("view")});
	SemanticKeywords.Add(TEXT("movement"), {TEXT("Movement"), TEXT("Move"), TEXT("Walk"), TEXT("Run"), TEXT("Jump")});
	SemanticKeywords.Add(TEXT("animation"), {TEXT("Anim"), TEXT("AnimInstance"), TEXT("ABP_"), TEXT("skeleton")});
	SemanticKeywords.Add(TEXT("audio"), {TEXT("Audio"), TEXT("Sound"), TEXT("Music"), TEXT("SFX")});
	SemanticKeywords.Add(TEXT("sound"), {TEXT("Sound"), TEXT("Audio"), TEXT("SFX")});
	SemanticKeywords.Add(TEXT("inventory"), {TEXT("Inventory"), TEXT("Item"), TEXT("Pickup"), TEXT("Slot")});
	SemanticKeywords.Add(TEXT("score"), {TEXT("Score"), TEXT("Points")});
	SemanticKeywords.Add(TEXT("save"), {TEXT("Save"), TEXT("Load"), TEXT("GameInstance"), TEXT("persist")});
	SemanticKeywords.Add(TEXT("menu"), {TEXT("Menu"), TEXT("WBP_"), TEXT("Widget"), TEXT("pause")});
	SemanticKeywords.Add(TEXT("ai"), {TEXT("AI"), TEXT("BehaviorTree"), TEXT("BT_"), TEXT("Blackboard"), TEXT("Task")});
	SemanticKeywords.Add(TEXT("behavior"), {TEXT("BehaviorTree"), TEXT("BT_"), TEXT("Blackboard")});
	SemanticKeywords.Add(TEXT("combat"), {TEXT("Combat"), TEXT("Attack"), TEXT("Damage"), TEXT("Fight"), TEXT("Melee"), TEXT("Weapon"), TEXT("Hit"), TEXT("Combo"), TEXT("Parry"), TEXT("Block"), TEXT("Dodge")});
	SemanticKeywords.Add(TEXT("melee"), {TEXT("Melee"), TEXT("Attack"), TEXT("Combo"), TEXT("Weapon"), TEXT("Sword"), TEXT("Hit")});
	SemanticKeywords.Add(TEXT("aggro"), {TEXT("Aggro"), TEXT("Agro"), TEXT("Threat"), TEXT("Target"), TEXT("Chase"), TEXT("Enemy"), TEXT("AI")});
	SemanticKeywords.Add(TEXT("spawn"), {TEXT("Spawn"), TEXT("BeginPlay"), TEXT("Construct")});
	SemanticKeywords.Add(TEXT("material"), {TEXT("Material"), TEXT("M_"), TEXT("texture"), TEXT("shader")});
	SemanticKeywords.Add(TEXT("particle"), {TEXT("Particle"), TEXT("Emitter"), TEXT("VFX"), TEXT("FX")});
	SemanticKeywords.Add(TEXT("physics"), {TEXT("Physics"), TEXT("Collision"), TEXT("Rigidbody")});
	SemanticKeywords.Add(TEXT("network"), {TEXT("Network"), TEXT("Replicate"), TEXT("Multiplayer"), TEXT("RPC")});
	SemanticKeywords.Add(TEXT("multiplayer"), {TEXT("Multiplayer"), TEXT("Network"), TEXT("Replicate"), TEXT("Server"), TEXT("Client")});
	SemanticKeywords.Add(TEXT("interact"), {TEXT("Interact"), TEXT("Interface"), TEXT("Use"), TEXT("Action")});
	SemanticKeywords.Add(TEXT("enum"), {TEXT("ENUM SUMMARY"), TEXT("enumeration"), TEXT("E_")});
	SemanticKeywords.Add(TEXT("struct"), {TEXT("STRUCT SUMMARY"), TEXT("structure"), TEXT("F_")});
	SemanticKeywords.Add(TEXT("interface"), {TEXT("INTERFACE SUMMARY"), TEXT("BPI_"), TEXT("contract")});
	SemanticKeywords.Add(TEXT("data"), {TEXT("DATA ASSET SUMMARY"), TEXT("DataAsset"), TEXT("DA_"), TEXT("config")});
	SemanticKeywords.Add(TEXT("table"), {TEXT("DATA TABLE SUMMARY"), TEXT("DataTable"), TEXT("DT_"), TEXT("row")});
	SemanticKeywords.Add(TEXT("anim"), {TEXT("AnimInstance"), TEXT("ABP_"), TEXT("skeleton"), TEXT("animation")});

	TArray<FString> ExpandedKeywords;
	for (const FString& Word : Keywords)
	{
		ExpandedKeywords.Add(Word);
		FString LowerWord = Word.ToLower();
		if (TArray<FString>* Related = SemanticKeywords.Find(LowerWord))
		{
			ExpandedKeywords.Append(*Related);
		}
	}

	TMap<FString, FString> RelevantContextMap;
	const int32 MaxContextEntries = 30;
	static constexpr int32 MaxContextTotalChars = 100000;

	auto Tokenize = [](const FString& In, TArray<FString>& Out)
	{
		FString Cur;
		Cur.Reserve(32);
		TCHAR Prev = 0;
		for (int32 i = 0; i < In.Len(); ++i)
		{
			const TCHAR C = In[i];
			if (FChar::IsAlnum(C))
			{
				if (Cur.Len() > 0 && FChar::IsUpper(C) && FChar::IsLower(Prev))
				{
					if (Cur.Len() >= 2) Out.Add(MoveTemp(Cur));
					Cur.Empty(32);
				}
				Cur.AppendChar(FChar::ToLower(C));
			}
			else if (Cur.Len() > 0)
			{
				if (Cur.Len() >= 2) Out.Add(MoveTemp(Cur));
				Cur.Empty(32);
			}
			Prev = C;
		}
		if (Cur.Len() >= 2) Out.Add(MoveTemp(Cur));
	};

	struct FDoc
	{
		FString Path;
		TMap<FString, int32> Tf;
		int32 DocLength = 0;
	};

	TArray<FDoc> Docs;
	TMap<FString, int32> DocFreq;
	int32 TotalLength = 0;

	for (const auto& Pair : ProjectIndexObject->Values)
	{
		const FString Key(*Pair.Key);
		if (Key.StartsWith(TEXT("_"))) continue;
		const TSharedPtr<FJsonObject>* Entry = nullptr;
		if (!Pair.Value.IsValid() || !Pair.Value->TryGetObject(Entry) || !Entry || !Entry->IsValid()) continue;

		FString ShortS, ParentS, TypeS;
		(*Entry)->TryGetStringField(UECPProjectIndex::FieldShortSummary, ShortS);
		(*Entry)->TryGetStringField(UECPProjectIndex::FieldParent, ParentS);
		(*Entry)->TryGetStringField(UECPProjectIndex::FieldType, TypeS);
		if (ShortS.IsEmpty()) ShortS = EntrySummary(Pair.Value);

		FDoc Doc;
		Doc.Path = Key;
		TArray<FString> Tokens;
		Tokenize(Key,     Tokens);
		Tokenize(ShortS,  Tokens);
		Tokenize(ParentS, Tokens);
		Tokenize(TypeS,   Tokens);
		Doc.DocLength = Tokens.Num();
		TotalLength += Doc.DocLength;

		TSet<FString> Seen;
		for (const FString& T : Tokens)
		{
			Doc.Tf.FindOrAdd(T)++;
			bool bAlready = false;
			Seen.Add(T, &bAlready);
			if (!bAlready) DocFreq.FindOrAdd(T)++;
		}
		Docs.Add(MoveTemp(Doc));
	}
	const double AvgDocLen = Docs.Num() > 0 ? (double)TotalLength / Docs.Num() : 1.0;
	const double N_DOCS    = (double)Docs.Num();

	TSet<FString> QuerySet;
	for (const FString& Word : ExpandedKeywords)
	{
		TArray<FString> Toks; Tokenize(Word, Toks);
		for (const FString& T : Toks) QuerySet.Add(T);
	}
	TArray<FString> QueryTerms = QuerySet.Array();

	constexpr double K1 = 1.5;
	constexpr double B  = 0.75;
	TArray<TPair<FString, double>> Scored;
	Scored.Reserve(Docs.Num());
	for (const FDoc& Doc : Docs)
	{
		double Score = 0.0;
		for (const FString& Q : QueryTerms)
		{
			const int32* TfPtr = Doc.Tf.Find(Q);
			if (!TfPtr || *TfPtr == 0) continue;
			const double Tf = (double)*TfPtr;
			const int32* DfPtr = DocFreq.Find(Q);
			const double Df  = DfPtr ? (double)*DfPtr : 0.0;
			const double Idf = FMath::Loge((N_DOCS - Df + 0.5) / (Df + 0.5) + 1.0);
			const double Norm = 1.0 - B + B * ((double)Doc.DocLength / AvgDocLen);
			Score += Idf * (Tf * (K1 + 1.0)) / (Tf + K1 * Norm);
		}
		if (Score > 0.0) Scored.Add({ Doc.Path, Score });
	}
	Scored.Sort([](const TPair<FString,double>& A, const TPair<FString,double>& B_) { return A.Value > B_.Value; });

	int32 ContextTotalChars = 0;
	for (const auto& Hit : Scored)
	{
		if (RelevantContextMap.Num() >= MaxContextEntries) break;
		if (ContextTotalChars >= MaxContextTotalChars)     break;
		TSharedPtr<FJsonValue> Val = ProjectIndexObject->TryGetField(Hit.Key);
		if (!Val.IsValid()) continue;
		const FString FullSummary = EntrySummary(Val);
		RelevantContextMap.Add(Hit.Key, FullSummary);
		ContextTotalChars += Hit.Key.Len() + FullSummary.Len();
	}

	const TSharedPtr<FJsonObject>* CountsObj = nullptr;
	ProjectIndexObject->TryGetObjectField(UECPProjectIndex::MetaCounts, CountsObj);
	auto ReadCount = [&CountsObj](const TCHAR* Key) -> double
	{
		double V = 0.0;
		if (CountsObj && CountsObj->IsValid()) (*CountsObj)->TryGetNumberField(Key, V);
		return V;
	};
	const double TotalAssets    = ReadCount(UECPProjectIndex::Count::Total);
	const double WidgetCount    = ReadCount(UECPProjectIndex::Count::Widget);
	const double ActorCount     = ReadCount(UECPProjectIndex::Count::Actor);
	const double AnimBPCount    = ReadCount(UECPProjectIndex::Count::AnimBp);
	const double BTCount        = ReadCount(UECPProjectIndex::Count::BehaviorTree);
	const double EnumCount      = ReadCount(UECPProjectIndex::Count::Enum);
	const double StructCount    = ReadCount(UECPProjectIndex::Count::Struct);
	const double InterfaceCount = ReadCount(UECPProjectIndex::Count::Interface);
	const double DataAssetCount = ReadCount(UECPProjectIndex::Count::DataAsset);
	const double DataTableCount = ReadCount(UECPProjectIndex::Count::DataTable);

	TSharedPtr<FJsonObject> ResultObject = MakeShareable(new FJsonObject());

	TSharedPtr<FJsonObject> OverviewObject = MakeShareable(new FJsonObject());
	OverviewObject->SetNumberField(TEXT("total_assets"), TotalAssets);
	OverviewObject->SetNumberField(TEXT("widgets"), WidgetCount);
	OverviewObject->SetNumberField(TEXT("actors"), ActorCount);
	OverviewObject->SetNumberField(TEXT("anim_blueprints"), AnimBPCount);
	OverviewObject->SetNumberField(TEXT("behavior_trees"), BTCount);
	OverviewObject->SetNumberField(TEXT("enums"), EnumCount);
	OverviewObject->SetNumberField(TEXT("structs"), StructCount);
	OverviewObject->SetNumberField(TEXT("interfaces"), InterfaceCount);
	OverviewObject->SetNumberField(TEXT("data_assets"), DataAssetCount);
	OverviewObject->SetNumberField(TEXT("data_tables"), DataTableCount);
	ResultObject->SetObjectField(TEXT("project_overview"), OverviewObject);

	ResultObject->SetNumberField(TEXT("matches_found"), RelevantContextMap.Num());

	TSharedPtr<FJsonObject> MatchesObject = MakeShareable(new FJsonObject());
	for (const auto& Pair : RelevantContextMap)
	{
		MatchesObject->SetStringField(Pair.Key, Pair.Value);
	}
	ResultObject->SetObjectField(TEXT("relevant_assets"), MatchesObject);

	if (RelevantContextMap.Num() == 0)
	{
		ResultObject->SetStringField(TEXT("message"), TEXT("No matches found for this query. Try different search terms, or use find_asset_by_name / list_assets_in_folder for direct browsing."));
	}

	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutJson);
	FJsonSerializer::Serialize(ResultObject.ToSharedRef(), Writer);
	return true;
}

void FUECPScannerCoordinator::BuildContext(const FString& UserQuery, FString& OutContext)
{
	OutContext = FString();
	FString Unused;
	QueryIndex(UserQuery, OutContext, Unused);
}

void FUECPScannerCoordinator::DoHeavyScan(const TArray<FAssetData>& AssetDataList, FOnScanComplete OnDone)
{
	TSharedPtr<FJsonObject> IndexObject = MakeShareable(new FJsonObject);
	FBtGraphDescriber BtDescriber;

	const TArray<FString> EnabledPrefixes = ScanTypeFilter::GetEnabledPathPrefixes();
	TArray<FAssetData> GameAssets;
	for (const FAssetData& Data : AssetDataList)
	{
		if (ScanTypeFilter::IsPathIncluded(Data.PackagePath.ToString(), EnabledPrefixes))
		{
			GameAssets.Add(Data);
		}
	}

	const int32 TotalAssets = GameAssets.Num();
	const int32 BatchSize = 25;
	int32 ProcessedCount = 0;
	int32 CacheHits = 0;

	const TSet<FString> PersistentSkipList = ScanCrashRecovery::LoadSkipList();

	TMap<FString, TSharedPtr<FJsonObject>> CachedEntries;
	{
		const FString PrevPath = FPaths::ProjectSavedDir() / TEXT("AI") / TEXT("ProjectIndex.json");
		FString PrevContent;
		if (FPaths::FileExists(PrevPath) && FFileHelper::LoadFileToString(PrevContent, *PrevPath))
		{
			TSharedPtr<FJsonObject> PrevIndex;
			const TSharedRef<TJsonReader<>> PrevReader = TJsonReaderFactory<>::Create(PrevContent);
			if (FJsonSerializer::Deserialize(PrevReader, PrevIndex) && PrevIndex.IsValid())
			{
				double PrevVersion = 0.0;
				PrevIndex->TryGetNumberField(UECPProjectIndex::MetaVersion, PrevVersion);
				if ((int32)PrevVersion == UECPProjectIndex::SchemaVersion)
				{
					for (const auto& Pair : PrevIndex->Values)
					{
						const FString Key(*Pair.Key);
						if (Key.StartsWith(TEXT("_"))) continue;
						if (!Pair.Value.IsValid()) continue;
						const TSharedPtr<FJsonObject>* Obj = nullptr;
						if (Pair.Value->TryGetObject(Obj) && Obj && Obj->IsValid())
							CachedEntries.Add(Key, *Obj);
					}
				}
			}
		}
	}

	const bool bAllow_Material       = ScanTypeFilter::IsTypeEnabled(TEXT("Material"), false);
	const bool bAllow_Texture        = ScanTypeFilter::IsTypeEnabled(TEXT("Texture"), false);
	const bool bAllow_StaticMesh     = ScanTypeFilter::IsTypeEnabled(TEXT("StaticMesh"), false);
	const bool bAllow_SkeletalMesh   = ScanTypeFilter::IsTypeEnabled(TEXT("SkeletalMesh"), false);
	const bool bAllow_AnimSequence   = ScanTypeFilter::IsTypeEnabled(TEXT("AnimSequence"), false);
	const bool bAllow_Sound          = ScanTypeFilter::IsTypeEnabled(TEXT("Sound"), false);
	const bool bAllow_Niagara        = ScanTypeFilter::IsTypeEnabled(TEXT("Niagara"), false);
	const bool bAllow_LevelSequence  = ScanTypeFilter::IsTypeEnabled(TEXT("LevelSequence"), false);
	const bool bAllow_ParticleSystem = ScanTypeFilter::IsTypeEnabled(TEXT("ParticleSystem"), false);
	const bool bAllow_PhysicsAsset   = ScanTypeFilter::IsTypeEnabled(TEXT("PhysicsAsset"), false);
	const bool bAllow_Curve          = ScanTypeFilter::IsTypeEnabled(TEXT("Curve"), false);
	int32 WidgetCount = 0;
	int32 ActorCount = 0;
	int32 AnimBPCount = 0;
	int32 BTCount = 0;
	int32 EnumCount = 0;
	int32 StructCount = 0;
	int32 InterfaceCount = 0;
	int32 DataAssetCount = 0;
	int32 DataTableCount = 0;
	int32 MaterialCount = 0;
	int32 TextureCount = 0;
	int32 StaticMeshCount = 0;
	int32 SkeletalMeshCount = 0;
	int32 OtherCount = 0;

	auto BumpCountersFromCachedEntry = [&](const TSharedPtr<FJsonObject>& Entry)
	{
		FString Type, Subtype;
		Entry->TryGetStringField(UECPProjectIndex::FieldType, Type);
		Entry->TryGetStringField(UECPProjectIndex::FieldSubtype, Subtype);
		if (Type == UECPProjectIndex::Type::Blueprint)
		{
			if      (Subtype == TEXT("Widget")) WidgetCount++;
			else if (Subtype == TEXT("AnimBP")) AnimBPCount++;
			else if (Subtype == TEXT("Actor"))  ActorCount++;
			else                                OtherCount++;
		}
		else if (Type == UECPProjectIndex::Type::Interface)    InterfaceCount++;
		else if (Type == UECPProjectIndex::Type::BehaviorTree) BTCount++;
		else if (Type == UECPProjectIndex::Type::Enum)         EnumCount++;
		else if (Type == UECPProjectIndex::Type::Struct)       StructCount++;
		else if (Type == UECPProjectIndex::Type::DataAsset)    DataAssetCount++;
		else if (Type == UECPProjectIndex::Type::DataTable)    DataTableCount++;
		else if (Type == UECPProjectIndex::Type::Material)     MaterialCount++;
		else if (Type == UECPProjectIndex::Type::Texture)      TextureCount++;
		else if (Type == UECPProjectIndex::Type::StaticMesh)   StaticMeshCount++;
		else if (Type == UECPProjectIndex::Type::SkeletalMesh) SkeletalMeshCount++;
		else                                                   OtherCount++;
	};

	TWeakPtr<SUECPMainWidget> ShellWeak = Shell;

	for (int32 BatchStart = 0; BatchStart < TotalAssets; BatchStart += BatchSize)
	{
		int32 BatchEnd = FMath::Min(BatchStart + BatchSize, TotalAssets);

		AsyncTask(ENamedThreads::GameThread, [ShellWeak, BatchStart, TotalAssets]()
		{
			if (TSharedPtr<SUECPMainWidget> Inner = ShellWeak.Pin())
				Inner->UpdateScanStatus(FString::Printf(TEXT("Scanning... %d/%d assets"), BatchStart, TotalAssets));
		});

		for (int32 i = BatchStart; i < BatchEnd; i++)
		{
			const FAssetData& Data = GameAssets[i];

			FString ClassName = Data.AssetClassPath.ToString();

			auto MatchesAny = [&ClassName](std::initializer_list<const TCHAR*> Names)
			{
				for (const TCHAR* N : Names) { if (ClassName.Contains(N)) return true; }
				return false;
			};

			if (!bAllow_Material       && MatchesAny({ TEXT("Material") })) { ProcessedCount++; continue; }
			if (!bAllow_Texture        && MatchesAny({ TEXT("Texture"), TEXT("MediaTexture") })) { ProcessedCount++; continue; }
			if (!bAllow_StaticMesh     && MatchesAny({ TEXT("StaticMesh") })) { ProcessedCount++; continue; }
			if (!bAllow_SkeletalMesh   && MatchesAny({ TEXT("SkeletalMesh") })) { ProcessedCount++; continue; }
			if (!bAllow_AnimSequence   && MatchesAny({ TEXT("AnimSequence"), TEXT("AnimMontage"), TEXT("AnimComposite"), TEXT("BlendSpace") })) { ProcessedCount++; continue; }
			if (!bAllow_Sound          && MatchesAny({ TEXT("SoundWave"), TEXT("SoundCue"), TEXT("SoundBase") })) { ProcessedCount++; continue; }
			if (!bAllow_Niagara        && MatchesAny({ TEXT("NiagaraSystem"), TEXT("NiagaraEmitter") })) { ProcessedCount++; continue; }
			if (!bAllow_LevelSequence  && MatchesAny({ TEXT("LevelSequence") })) { ProcessedCount++; continue; }
			if (!bAllow_ParticleSystem && MatchesAny({ TEXT("ParticleSystem") })) { ProcessedCount++; continue; }
			if (!bAllow_PhysicsAsset   && MatchesAny({ TEXT("PhysicsAsset") })) { ProcessedCount++; continue; }
			if (!bAllow_Curve          && MatchesAny({ TEXT("CurveFloat"), TEXT("CurveVector"), TEXT("CurveLinearColor") })) { ProcessedCount++; continue; }

			if (MatchesAny({ TEXT("MediaSource"), TEXT("FoliageType"), TEXT("LandscapeGrass"), TEXT("Font"), TEXT("SlateBrush"), TEXT("PaperSprite") }))
			{
				ProcessedCount++;
				continue;
			}

			FString AssetPathForLog = Data.GetSoftObjectPath().ToString();

			if (PersistentSkipList.Contains(AssetPathForLog))
			{
				UE_LOG(LogTemp, Log, TEXT("Skipping '%s' (in persistent crash skip list)"), *AssetPathForLog);
				ProcessedCount++;
				continue;
			}

			if (TSharedPtr<FJsonObject>* CachedEntry = CachedEntries.Find(AssetPathForLog))
			{
				double CachedMtime = 0.0;
				(*CachedEntry)->TryGetNumberField(UECPProjectIndex::FieldMtime, CachedMtime);
				const int64 CurrentMtime = GetAssetMtime(Data);
				if (CachedMtime > 0 && CurrentMtime > 0 && (int64)CachedMtime == CurrentMtime)
				{
					IndexObject->SetObjectField(AssetPathForLog, *CachedEntry);
					BumpCountersFromCachedEntry(*CachedEntry);
					CacheHits++;
					ProcessedCount++;
					continue;
				}
			}

			ScanCrashRecovery::MarkScanning(AssetPathForLog);

			FString Summary;
			FString ShortSummary;
			FString AssetPathName;
			FString EntryType;
			FString EntrySubtype;
			FString EntryParent;
			int32 EntryComplexity = 0;
			TArray<FBpIssue> EntryIssues;
			FGraphEventRef GameThreadTask = FFunctionGraphTask::CreateAndDispatchWhenReady(
				[AssetDataToLoad = Data, &Summary, &ShortSummary, &AssetPathName, &EntryType, &EntrySubtype, &EntryParent, &EntryComplexity, &EntryIssues, &BtDescriber,
				 &WidgetCount, &ActorCount, &AnimBPCount, &BTCount, &EnumCount, &StructCount,
				 &InterfaceCount, &DataAssetCount, &DataTableCount,
				 &MaterialCount, &TextureCount, &StaticMeshCount, &SkeletalMeshCount, &OtherCount]()
				{
					if (FAssetCompilingManager::Get().GetNumRemainingAssets() > 0)
					{
						FAssetCompilingManager::Get().FinishAllCompilation();
					}

					const FString AssetPath = AssetDataToLoad.GetSoftObjectPath().ToString();
					UObject* LoadedAsset = FindObject<UObject>(nullptr, *AssetPath);
					if (!LoadedAsset)
					{
						LoadedAsset = LoadObject<UObject>(nullptr, *AssetPath, nullptr, LOAD_Quiet | LOAD_NoWarn);
					}

					if (!IsValid(LoadedAsset)) return;

					AssetPathName = LoadedAsset->GetPathName();

					auto SafeJoin = [](const TArray<FString>& Arr, const TCHAR* Delim) -> FString
					{
						TArray<FString> Valid;
						Valid.Reserve(Arr.Num());
						for (const FString& S : Arr)
						{
							if (!S.IsEmpty()) Valid.Add(S);
						}
						return FString::Join(Valid, Delim);
					};

					if (UBlueprint* Blueprint = Cast<UBlueprint>(LoadedAsset))
					{
						if (Blueprint->BlueprintType == BPTYPE_Interface)
						{
							TArray<FString> Functions;
							for (const auto& Func : Blueprint->FunctionGraphs)
							{
								if (Func) Functions.Add(Func->GetName());
							}
							Summary = FString::Printf(TEXT("--- INTERFACE SUMMARY ---\nName: %s\nFunctions: %s"),
								*Blueprint->GetName(),
								*SafeJoin(Functions, TEXT(", ")));
							EntryType = UECPProjectIndex::Type::Interface;
							InterfaceCount++;
						}
						else
						{
							FBpSummarizer Summarizer;
							Summary = Summarizer.SummarizeWithIssues(Blueprint, EntryIssues);
							EntryComplexity = Summarizer.ComputeComplexityScore(Blueprint);
							ShortSummary = Summarizer.ComputeShortSummary(Blueprint);
							EntryType = UECPProjectIndex::Type::Blueprint;
							if (Blueprint->ParentClass)
							{
								EntryParent = Blueprint->ParentClass->GetName();
								if (EntryParent.Contains(TEXT("UserWidget")) || EntryParent.Contains(TEXT("Widget"))) { EntrySubtype = TEXT("Widget"); WidgetCount++; }
								else if (EntryParent.Contains(TEXT("AnimInstance")))                                    { EntrySubtype = TEXT("AnimBP"); AnimBPCount++; }
								else if (EntryParent.Contains(TEXT("Actor")))                                           { EntrySubtype = TEXT("Actor");  ActorCount++; }
								else                                                                                    { EntrySubtype = TEXT("Other");  OtherCount++; }
							}
							else
							{
								EntrySubtype = TEXT("Other");
								OtherCount++;
							}
						}
					}
					else if (UBehaviorTree* BehaviorTree = Cast<UBehaviorTree>(LoadedAsset))
					{
						Summary = BtDescriber.Describe(BehaviorTree);
						EntryType = UECPProjectIndex::Type::BehaviorTree;
						BTCount++;
					}
					else if (UUserDefinedEnum* UserEnum = Cast<UUserDefinedEnum>(LoadedAsset))
					{
						TArray<FString> EnumValues;
						for (int32 Idx = 0; Idx < UserEnum->NumEnums(); Idx++)
						{
							FString DisplayName = UserEnum->GetDisplayNameTextByIndex(Idx).ToString();
							if (!DisplayName.IsEmpty()) EnumValues.Add(DisplayName);
						}
						Summary = FString::Printf(TEXT("--- ENUM SUMMARY ---\nName: %s\nValues: %s"),
							*UserEnum->GetName(),
							*SafeJoin(EnumValues, TEXT(", ")));
						EntryType = UECPProjectIndex::Type::Enum;
						EnumCount++;
					}
					else if (UUserDefinedStruct* UserStruct = Cast<UUserDefinedStruct>(LoadedAsset))
					{
						TArray<FString> StructMembers;
						const TArray<FStructVariableDescription>& VarDescs = FStructureEditorUtils::GetVarDesc(UserStruct);
						for (const FStructVariableDescription& VarDesc : VarDescs)
						{
							StructMembers.Add(VarDesc.VarName.ToString());
						}
						Summary = FString::Printf(TEXT("--- STRUCT SUMMARY ---\nName: %s\nMembers: %s"),
							*UserStruct->GetName(),
							*SafeJoin(StructMembers, TEXT(", ")));
						EntryType = UECPProjectIndex::Type::Struct;
						StructCount++;
					}
					else if (UDataAsset* DataAsset = Cast<UDataAsset>(LoadedAsset))
					{
						TArray<FString> Properties;
						for (TFieldIterator<FProperty> PropIt(DataAsset->GetClass()); PropIt; ++PropIt)
						{
							FProperty* Prop = *PropIt;
							if (!Prop->IsA<FObjectProperty>() && !Prop->GetName().StartsWith(TEXT("_")))
							{
								Properties.Add(FString::Printf(TEXT("%s (%s)"), *Prop->GetName(), *Prop->GetClass()->GetName()));
							}
						}
						Summary = FString::Printf(TEXT("--- DATA ASSET SUMMARY ---\nName: %s\nClass: %s\nProperties: %s"),
							*DataAsset->GetName(),
							*DataAsset->GetClass()->GetName(),
							*SafeJoin(Properties, TEXT(", ")));
						EntryType = UECPProjectIndex::Type::DataAsset;
						DataAssetCount++;
					}
					else if (UDataTable* DataTable = Cast<UDataTable>(LoadedAsset))
					{
						const UScriptStruct* RowStruct = DataTable->GetRowStruct();
						TArray<FString> Columns;
						if (RowStruct)
						{
							if (const UUserDefinedStruct* RowUserStruct = Cast<UUserDefinedStruct>(RowStruct))
							{
								const TArray<FStructVariableDescription>& VarDescs = FStructureEditorUtils::GetVarDesc(RowUserStruct);
								for (const FStructVariableDescription& VarDesc : VarDescs)
								{
									Columns.Add(VarDesc.VarName.ToString());
								}
							}
							else
							{
								for (TFieldIterator<FProperty> PropIt(RowStruct); PropIt; ++PropIt)
								{
									Columns.Add(PropIt->GetName());
								}
							}
						}
						Summary = FString::Printf(TEXT("--- DATA TABLE SUMMARY ---\nName: %s\nRow Type: %s\nColumns: %s"),
							*DataTable->GetName(),
							RowStruct ? *RowStruct->GetName() : TEXT("Unknown"),
							*SafeJoin(Columns, TEXT(", ")));
						EntryType = UECPProjectIndex::Type::DataTable;
						DataTableCount++;
					}
					else if (UTexture* Texture = Cast<UTexture>(LoadedAsset))
					{
						SummarizeTexture(Texture, Summary, EntryIssues, EntryComplexity);
						EntryType = UECPProjectIndex::Type::Texture;
						TextureCount++;
					}
					else if (UStaticMesh* StaticMesh = Cast<UStaticMesh>(LoadedAsset))
					{
						SummarizeStaticMesh(StaticMesh, Summary, EntryIssues, EntryComplexity);
						EntryType = UECPProjectIndex::Type::StaticMesh;
						StaticMeshCount++;
					}
					else if (USkeletalMesh* SkeletalMesh = Cast<USkeletalMesh>(LoadedAsset))
					{
						SummarizeSkeletalMesh(SkeletalMesh, Summary, EntryIssues, EntryComplexity);
						EntryType = UECPProjectIndex::Type::SkeletalMesh;
						SkeletalMeshCount++;
					}
					else if (UMaterialInterface* MatInterface = Cast<UMaterialInterface>(LoadedAsset))
					{
						SummarizeMaterial(MatInterface, Summary, EntryIssues, EntryComplexity);
						EntryType = UECPProjectIndex::Type::Material;
						MaterialCount++;
					}
					else
					{
						OtherCount++;
					}
				}, TStatId(), nullptr, ENamedThreads::GameThread);

			FTaskGraphInterface::Get().WaitUntilTaskCompletes(GameThreadTask);

			ScanCrashRecovery::ClearScanning();

			if (!Summary.IsEmpty() && !AssetPathName.IsEmpty() && !EntryType.IsEmpty())
			{
				TSharedPtr<FJsonObject> Entry = MakeIndexEntry(EntryType, Summary);
				if (!EntrySubtype.IsEmpty()) Entry->SetStringField(UECPProjectIndex::FieldSubtype, EntrySubtype);
				if (!EntryParent.IsEmpty())  Entry->SetStringField(UECPProjectIndex::FieldParent, EntryParent);
				if (EntryComplexity > 0)     Entry->SetNumberField(UECPProjectIndex::FieldComplexity, EntryComplexity);
				Entry->SetNumberField(UECPProjectIndex::FieldMtime, (double)GetAssetMtime(Data));
				const FString FinalShort = ShortSummary.IsEmpty() ? DeriveShortSummary(Summary) : ShortSummary;
				if (!FinalShort.IsEmpty()) Entry->SetStringField(UECPProjectIndex::FieldShortSummary, FinalShort);
				AttachIssuesToEntry(Entry.ToSharedRef(), EntryIssues);
				IndexObject->SetObjectField(AssetPathName, Entry);
			}

			ProcessedCount++;
		}

		FPlatformProcess::Sleep(0.005f);
	}

	int32 LevelCount = 0;
	{
		TArray<FAssetData> WorldAssets;
		FGraphEventRef GTTask = FFunctionGraphTask::CreateAndDispatchWhenReady(
			[&WorldAssets]()
			{
				FAssetRegistryModule& Reg = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
				Reg.Get().GetAssetsByClass(UWorld::StaticClass()->GetClassPathName(), WorldAssets, true);
			}, TStatId(), nullptr, ENamedThreads::GameThread);
		GTTask->Wait();

		bool bSkipTempLevels = true;
		GConfig->GetBool(TEXT("BpGeneratorUltimate"), TEXT("Scan_SkipPlayInEditorLevels"), bSkipTempLevels, GEditorPerProjectIni);

		for (const FAssetData& WorldData : WorldAssets)
		{
			const FString PkgPath = WorldData.PackagePath.ToString();
			if (!ScanTypeFilter::IsPathIncluded(PkgPath, EnabledPrefixes)) continue;

			if (bSkipTempLevels)
			{
				const FString AssetName = WorldData.AssetName.ToString();
				if (AssetName.StartsWith(TEXT("UEDPIE_"))
					|| AssetName.StartsWith(TEXT("Untitled"))
					|| PkgPath.StartsWith(TEXT("/Temp/"))
					|| PkgPath.Contains(TEXT("/Transient/")))
				{
					continue;
				}
			}

			FString LevelSummary = FString::Printf(TEXT("Level asset: %s\nPath: %s\nPackage: %s"),
				*WorldData.AssetName.ToString(),
				*WorldData.GetObjectPathString(),
				*WorldData.PackagePath.ToString());
			FString TagVal;
			if (WorldData.GetTagValue(FName("ActorCount"), TagVal) && !TagVal.IsEmpty())
				LevelSummary += FString::Printf(TEXT("\nActorCount: %s"), *TagVal);
			TSharedPtr<FJsonObject> LevelEntry = MakeIndexEntry(UECPProjectIndex::Type::Level, LevelSummary);
			LevelEntry->SetNumberField(UECPProjectIndex::FieldMtime, (double)GetAssetMtime(WorldData));
			LevelEntry->SetStringField(UECPProjectIndex::FieldShortSummary, DeriveShortSummary(LevelSummary));
			IndexObject->SetObjectField(WorldData.GetObjectPathString(), LevelEntry);
			LevelCount++;
		}
	}

	int32 CppClassCount = 0, CppStructCount = 0, CppEnumCount = 0, CppFileCount = 0;

	auto ScanSourceDirectory = [&](const FString& SourceDir, const FString& OriginLabel, const FString& PathKeyPrefix)
	{
		if (!FPaths::DirectoryExists(SourceDir)) return;

		AsyncTask(ENamedThreads::GameThread, [ShellWeak, OriginLabel]()
		{
			if (TSharedPtr<SUECPMainWidget> Inner = ShellWeak.Pin())
				Inner->UpdateScanStatus(FString::Printf(TEXT("Scanning C++ source: %s..."), *OriginLabel));
		});

		TArray<FString> HeaderFiles;
		IFileManager::Get().FindFilesRecursive(HeaderFiles, *SourceDir, TEXT("*.h"), true, false);

			auto ExtractTypeName = [](const FString& DeclLine) -> FString
			{
				FString After = DeclLine;
				int32 KwIdx = After.Find(TEXT("class "));
				if (KwIdx < 0) KwIdx = After.Find(TEXT("struct "));
				if (KwIdx >= 0) After = After.Mid(KwIdx + 6).TrimStart();
				if (After.Contains(TEXT("_API ")))
				{
					int32 ApiEnd = After.Find(TEXT("_API "));
					After = After.Mid(ApiEnd + 5).TrimStart();
				}
				int32 StopIdx = After.Len();
				int32 Tmp;
				if (After.FindChar(TEXT(':'), Tmp) && Tmp < StopIdx) StopIdx = Tmp;
				if (After.FindChar(TEXT('{'), Tmp) && Tmp < StopIdx) StopIdx = Tmp;
				if (After.FindChar(TEXT(' '), Tmp) && Tmp < StopIdx) StopIdx = Tmp;
				return After.Left(StopIdx).TrimEnd();
			};

			auto ExtractParent = [](const FString& DeclLine) -> FString
			{
				int32 ColonIdx;
				if (!DeclLine.FindChar(TEXT(':'), ColonIdx)) return TEXT("");
				FString ParentPart = DeclLine.Mid(ColonIdx + 1).TrimStart();
				ParentPart.ReplaceInline(TEXT("public "), TEXT(""));
				ParentPart.ReplaceInline(TEXT("protected "), TEXT(""));
				ParentPart.ReplaceInline(TEXT("private "), TEXT(""));
				ParentPart.ReplaceInline(TEXT("{"), TEXT(""));
				int32 CommaIdx;
				if (ParentPart.FindChar(TEXT(','), CommaIdx)) ParentPart = ParentPart.Left(CommaIdx);
				return ParentPart.TrimStartAndEnd();
			};

			for (const FString& HeaderPath : HeaderFiles)
			{
				FString FileContent;
				if (!FFileHelper::LoadFileToString(FileContent, *HeaderPath)) continue;

				if (!FileContent.Contains(TEXT("UCLASS")) && !FileContent.Contains(TEXT("USTRUCT")) && !FileContent.Contains(TEXT("UENUM")))
					continue;

				CppFileCount++;
				FString FileName = FPaths::GetCleanFilename(HeaderPath);
				FString RelPath = HeaderPath;
				FPaths::MakePathRelativeTo(RelPath, *FPaths::ProjectDir());

				FString Summary = FString::Printf(TEXT("--- C++ HEADER ---\nOrigin: %s\nFile: %s\nPath: %s\n"), *OriginLabel, *FileName, *RelPath);

				TArray<FString> Lines;
				FileContent.ParseIntoArrayLines(Lines);

				TArray<FString> Includes, Properties, Functions, Delegates, PublicMethods;
				FString CurrentClass, CurrentParent;
				bool bInPublic = false;
				int32 BraceDepth = 0;
				bool bInsideClass = false;

				for (int32 LineIdx = 0; LineIdx < Lines.Num(); LineIdx++)
				{
					FString Line = Lines[LineIdx].TrimStartAndEnd();

					if (Line.StartsWith(TEXT("#include \"")))
					{
						FString Inc = Line.Mid(10);
						int32 CloseQuote;
						if (Inc.FindChar(TEXT('"'), CloseQuote)) Inc = Inc.Left(CloseQuote);
						if (!Inc.Contains(TEXT(".generated.h")) && Inc != TEXT("CoreMinimal.h"))
							Includes.Add(Inc);
						continue;
					}

					if (Line.StartsWith(TEXT("public:"))) { bInPublic = true; continue; }
					if (Line.StartsWith(TEXT("protected:")) || Line.StartsWith(TEXT("private:"))) { bInPublic = false; continue; }

					if (Line.StartsWith(TEXT("UCLASS")))
					{
						bInsideClass = true; bInPublic = false;
						for (int32 j = LineIdx; j < FMath::Min(LineIdx + 3, Lines.Num()); j++)
						{
							FString CL = Lines[j].TrimStartAndEnd();
							if (CL.Contains(TEXT("class ")) && (CL.Contains(TEXT(":")) || CL.Contains(TEXT("{"))))
							{
								CurrentClass = ExtractTypeName(CL);
								CurrentParent = ExtractParent(CL);
								if (!CurrentClass.IsEmpty())
								{
									Summary += FString::Printf(TEXT("\nUCLASS: %s"), *CurrentClass);
									if (!CurrentParent.IsEmpty()) Summary += FString::Printf(TEXT(" (Parent: %s)"), *CurrentParent);
									CppClassCount++;
								}
								break;
							}
						}
					}
					else if (Line.StartsWith(TEXT("USTRUCT")))
					{
						for (int32 j = LineIdx; j < FMath::Min(LineIdx + 3, Lines.Num()); j++)
						{
							if (Lines[j].Contains(TEXT("struct ")))
							{
								FString SN = ExtractTypeName(Lines[j].TrimStartAndEnd());
								if (!SN.IsEmpty()) { Summary += FString::Printf(TEXT("\nUSTRUCT: %s"), *SN); CppStructCount++; }
								break;
							}
						}
					}
					else if (Line.StartsWith(TEXT("UENUM")))
					{
						for (int32 j = LineIdx; j < FMath::Min(LineIdx + 3, Lines.Num()); j++)
						{
							FString EL = Lines[j].TrimStartAndEnd();
							if (EL.Contains(TEXT("enum ")))
							{
								FString After = EL.Mid(EL.Find(TEXT("enum ")) + 5).TrimStart();
								After.ReplaceInline(TEXT("class "), TEXT(""));
								int32 Ci; if (After.FindChar(TEXT(':'), Ci)) After = After.Left(Ci);
								After.ReplaceInline(TEXT("{"), TEXT(""));
								FString EN = After.TrimStartAndEnd();
								int32 Si; if (EN.FindChar(TEXT(' '), Si)) EN = EN.Left(Si);
								if (!EN.IsEmpty()) { Summary += FString::Printf(TEXT("\nUENUM: %s"), *EN); CppEnumCount++; }
								break;
							}
						}
					}
					else if (Line.StartsWith(TEXT("UPROPERTY")))
					{
						if (LineIdx + 1 < Lines.Num())
						{
							FString PL = Lines[LineIdx + 1].TrimStartAndEnd();
							int32 Si; if (PL.FindChar(TEXT(';'), Si)) PL = PL.Left(Si);
							int32 Ei; if (PL.FindChar(TEXT('='), Ei)) PL = PL.Left(Ei);
							PL = PL.TrimEnd();
							if (!PL.IsEmpty()) Properties.Add(PL);
						}
					}
					else if (Line.StartsWith(TEXT("UFUNCTION")))
					{
						for (int32 j = LineIdx + 1; j < FMath::Min(LineIdx + 3, Lines.Num()); j++)
						{
							FString FL = Lines[j].TrimStartAndEnd();
							if (FL.Contains(TEXT("(")) && !FL.StartsWith(TEXT("UFUNCTION")) && !FL.StartsWith(TEXT("//")))
							{
								int32 Si; if (FL.FindChar(TEXT(';'), Si)) FL = FL.Left(Si);
								if (!FL.IsEmpty()) Functions.Add(FL);
								break;
							}
						}
					}
					else if (Line.Contains(TEXT("DECLARE_DYNAMIC")) || Line.Contains(TEXT("DECLARE_MULTICAST")))
					{
						int32 PS = Line.Find(TEXT("("));
						if (PS >= 0)
						{
							FString AP = Line.Mid(PS + 1);
							int32 Cm; if (AP.FindChar(TEXT(','), Cm)) AP = AP.Left(Cm);
							int32 CP; if (AP.FindChar(TEXT(')'), CP)) AP = AP.Left(CP);
							if (!AP.TrimStartAndEnd().IsEmpty()) Delegates.Add(AP.TrimStartAndEnd());
						}
					}
					else if (bInPublic && bInsideClass && Line.Contains(TEXT("(")) && Line.Contains(TEXT(";"))
						&& !Line.StartsWith(TEXT("GENERATED")) && !Line.StartsWith(TEXT("//"))
						&& !Line.StartsWith(TEXT("DECLARE_")) && !Line.StartsWith(TEXT("friend"))
						&& !Line.Contains(TEXT("UFUNCTION")) && !Line.Contains(TEXT("UPROPERTY")))
					{
						FString ML = Line;
						int32 Si; if (ML.FindChar(TEXT(';'), Si)) ML = ML.Left(Si);
						ML = ML.TrimEnd();
						if (!ML.IsEmpty() && ML.Len() < 200) PublicMethods.Add(ML);
					}
				}

				if (Includes.Num() > 0)
				{
					Summary += TEXT("\nIncludes: ") + FString::Join(Includes, TEXT(", "));
				}
				if (Properties.Num() > 0)
				{
					Summary += TEXT("\nProperties:");
					for (const FString& P : Properties) Summary += FString::Printf(TEXT("\n  %s"), *P);
				}
				if (Functions.Num() > 0)
				{
					Summary += TEXT("\nUFunctions:");
					for (const FString& F : Functions) Summary += FString::Printf(TEXT("\n  %s"), *F);
				}
				if (PublicMethods.Num() > 0)
				{
					Summary += TEXT("\nPublic Methods:");
					for (const FString& M : PublicMethods) Summary += FString::Printf(TEXT("\n  %s"), *M);
				}
				if (Delegates.Num() > 0)
				{
					Summary += TEXT("\nDelegates: ") + FString::Join(Delegates, TEXT(", "));
				}

				FString CppPath = FPaths::ChangeExtension(HeaderPath, TEXT("cpp"));
				FString CppContent;
				if (FFileHelper::LoadFileToString(CppContent, *CppPath))
				{
					TArray<FString> CppIncludes;
					TArray<FString> CppLines;
					CppContent.ParseIntoArrayLines(CppLines);
					TSet<FString> CastsUsed, ComponentsUsed, SpawnCalls;

					for (const FString& CL : CppLines)
					{
						FString TL = CL.TrimStartAndEnd();
						if (TL.StartsWith(TEXT("#include \"")))
						{
							FString Inc = TL.Mid(10);
							int32 CQ; if (Inc.FindChar(TEXT('"'), CQ)) Inc = Inc.Left(CQ);
							if (!Inc.Contains(TEXT(".generated.h")) && Inc != TEXT("CoreMinimal.h") && Inc != FileName)
								CppIncludes.Add(Inc);
						}
						int32 CastPos = TL.Find(TEXT("Cast<"));
						if (CastPos >= 0)
						{
							FString After = TL.Mid(CastPos + 5);
							int32 GT; if (After.FindChar(TEXT('>'), GT)) CastsUsed.Add(After.Left(GT));
						}
						if (TL.Contains(TEXT("FindComponentByClass<")) || TL.Contains(TEXT("GetComponentByClass<")))
						{
							int32 Angle = TL.Find(TEXT("ByClass<"));
							if (Angle >= 0)
							{
								FString After = TL.Mid(Angle + 8);
								int32 GT; if (After.FindChar(TEXT('>'), GT)) ComponentsUsed.Add(After.Left(GT));
							}
						}
						if (TL.Contains(TEXT("CreateDefaultSubobject<")))
						{
							int32 Angle = TL.Find(TEXT("Subobject<"));
							if (Angle >= 0)
							{
								FString After = TL.Mid(Angle + 10);
								int32 GT; if (After.FindChar(TEXT('>'), GT)) ComponentsUsed.Add(After.Left(GT));
							}
						}
						if (TL.Contains(TEXT("SpawnActor<")))
						{
							int32 Angle = TL.Find(TEXT("SpawnActor<"));
							if (Angle >= 0)
							{
								FString After = TL.Mid(Angle + 11);
								int32 GT; if (After.FindChar(TEXT('>'), GT)) SpawnCalls.Add(After.Left(GT));
							}
						}
					}

					if (CppIncludes.Num() > 0)
						Summary += TEXT("\nCpp Includes: ") + FString::Join(CppIncludes, TEXT(", "));
					if (CastsUsed.Num() > 0)
						Summary += TEXT("\nCasts To: ") + FString::Join(CastsUsed.Array(), TEXT(", "));
					if (ComponentsUsed.Num() > 0)
						Summary += TEXT("\nUses Components: ") + FString::Join(ComponentsUsed.Array(), TEXT(", "));
					if (SpawnCalls.Num() > 0)
						Summary += TEXT("\nSpawns: ") + FString::Join(SpawnCalls.Array(), TEXT(", "));
				}

				TSharedPtr<FJsonObject> CppEntry = MakeIndexEntry(UECPProjectIndex::Type::CppHeader, Summary);
				const FDateTime FileStamp = IFileManager::Get().GetTimeStamp(*HeaderPath);
				if (FileStamp != FDateTime::MinValue())
				{
					CppEntry->SetNumberField(UECPProjectIndex::FieldMtime, (double)FileStamp.ToUnixTimestamp());
				}
				CppEntry->SetStringField(UECPProjectIndex::FieldShortSummary, DeriveShortSummary(Summary));
				IndexObject->SetObjectField(FString::Printf(TEXT("%s%s"), *PathKeyPrefix, *RelPath), CppEntry);
			}
		};

	{
		bool bScanProjectSource = true;
		GConfig->GetBool(TEXT("BpGeneratorUltimate"), TEXT("Scan_ProjectSource"), bScanProjectSource, GEditorPerProjectIni);
		if (bScanProjectSource)
		{
			ScanSourceDirectory(FPaths::Combine(FPaths::ProjectDir(), TEXT("Source")), TEXT("Project"), TEXT("cpp://"));
		}
	}

	{
		IPluginManager& PluginMgr = IPluginManager::Get();
		TArray<TSharedRef<IPlugin>> AllPlugins = PluginMgr.GetEnabledPlugins();
		auto SanitizeKey = [](const FString& In) { FString Out = In; Out.ReplaceInline(TEXT("/"), TEXT("_")); Out.ReplaceInline(TEXT("\\"), TEXT("_")); Out.ReplaceInline(TEXT(" "), TEXT("_")); Out.ReplaceInline(TEXT("."), TEXT("_")); return Out; };
		for (const TSharedRef<IPlugin>& P : AllPlugins)
		{
			const FString Name = P->GetName();
			const FString PluginSource = FPaths::Combine(P->GetBaseDir(), TEXT("Source"));
			if (!FPaths::DirectoryExists(PluginSource)) continue;

			bool bEnabled = false;
			GConfig->GetBool(TEXT("BpGeneratorUltimate"),
				*FString::Printf(TEXT("ScanPluginSource_%s"), *SanitizeKey(Name)),
				bEnabled, GEditorPerProjectIni);
			if (!bEnabled) continue;

			const FString KeyPrefix = FString::Printf(TEXT("cpp-plugin://%s/"), *Name);
			ScanSourceDirectory(PluginSource, FString::Printf(TEXT("Plugin: %s"), *Name), KeyPrefix);
		}
	}

	IndexObject->SetNumberField(UECPProjectIndex::MetaVersion, UECPProjectIndex::SchemaVersion);
	IndexObject->SetStringField(UECPProjectIndex::MetaScanTimestamp, FDateTime::UtcNow().ToString());

	TSharedPtr<FJsonObject> Counts = MakeShareable(new FJsonObject);
	Counts->SetNumberField(UECPProjectIndex::Count::Total,        ProcessedCount + LevelCount + CppFileCount);
	Counts->SetNumberField(UECPProjectIndex::Count::Widget,       WidgetCount);
	Counts->SetNumberField(UECPProjectIndex::Count::Actor,        ActorCount);
	Counts->SetNumberField(UECPProjectIndex::Count::AnimBp,       AnimBPCount);
	Counts->SetNumberField(UECPProjectIndex::Count::BehaviorTree, BTCount);
	Counts->SetNumberField(UECPProjectIndex::Count::Enum,         EnumCount);
	Counts->SetNumberField(UECPProjectIndex::Count::Struct,       StructCount);
	Counts->SetNumberField(UECPProjectIndex::Count::Interface,    InterfaceCount);
	Counts->SetNumberField(UECPProjectIndex::Count::DataAsset,    DataAssetCount);
	Counts->SetNumberField(UECPProjectIndex::Count::DataTable,    DataTableCount);
	Counts->SetNumberField(UECPProjectIndex::Count::Level,        LevelCount);
	Counts->SetNumberField(UECPProjectIndex::Count::CppFile,      CppFileCount);
	Counts->SetNumberField(UECPProjectIndex::Count::CppClass,     CppClassCount);
	Counts->SetNumberField(UECPProjectIndex::Count::CppStruct,    CppStructCount);
	Counts->SetNumberField(UECPProjectIndex::Count::CppEnum,      CppEnumCount);
	Counts->SetNumberField(UECPProjectIndex::Count::Material,     MaterialCount);
	Counts->SetNumberField(UECPProjectIndex::Count::Texture,      TextureCount);
	Counts->SetNumberField(UECPProjectIndex::Count::StaticMesh,   StaticMeshCount);
	Counts->SetNumberField(UECPProjectIndex::Count::SkeletalMesh, SkeletalMeshCount);
	Counts->SetNumberField(UECPProjectIndex::Count::Other,        OtherCount);
	IndexObject->SetObjectField(UECPProjectIndex::MetaCounts, Counts);

	FString OutputString;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutputString);
	FJsonSerializer::Serialize(IndexObject.ToSharedRef(), Writer);
	const FString SavePath = FPaths::ProjectSavedDir() / TEXT("AI") / TEXT("ProjectIndex.json");
	IFileManager::Get().MakeDirectory(*FPaths::GetPath(SavePath), true);
	FString ErrorMessage;
	if (!FFileHelper::SaveStringToFile(OutputString, *SavePath))
	{
		ErrorMessage = FString::Printf(TEXT("Failed to save Project Index file to %s"), *SavePath);
	}

	if (ErrorMessage.IsEmpty())
	{
		WriteScanSnapshot(IndexObject);
	}

	ScanCrashRecovery::ClearScanning();

	if (OnDone)
	{
		FString CompletionMessage;
		if (ErrorMessage.IsEmpty())
		{
			const FString CacheNote = (CacheHits > 0)
				? FString::Printf(TEXT(" (%d unchanged, reused from cache)"), CacheHits)
				: FString();
			CompletionMessage = FString::Printf(
				TEXT("Scan complete — %d assets%s + %d C++ files indexed\nWidgets: %d  Actors: %d  Anim BPs: %d  BTs: %d  C++ Classes: %d"),
				ProcessedCount, *CacheNote, CppFileCount, WidgetCount, ActorCount, AnimBPCount, BTCount, CppClassCount);
		}
		else
		{
			CompletionMessage = ErrorMessage;
		}
		OnDone(ErrorMessage.IsEmpty(), CompletionMessage);
	}
}
