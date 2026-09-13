// Copyright 2026, BlueprintsLab, All rights reserved

#include "Meshy/MeshyAssetImporter.h"
#include "UECPAssetGenModule.h"
#include "Utils/MountResolver.h"
#include "Animation/AnimSequence.h"
#include "Animation/Skeleton.h"
#include "AssetExportTask.h"
#include "AssetImportTask.h"
#include "AssetToolsModule.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "EditorAssetLibrary.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "Exporters/Exporter.h"
#include "Factories/FbxAnimSequenceImportData.h"
#include "Factories/FbxFactory.h"
#include "Factories/FbxImportUI.h"
#include "Factories/FbxSkeletalMeshImportData.h"
#include "Factories/TextureFactory.h"
#include "IImageWrapperModule.h"
#include "IImageWrapper.h"
#include "Misc/Base64.h"
#include "Misc/FileHelper.h"
#include "Misc/Guid.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "UObject/UObjectIterator.h"
#include <initializer_list>

namespace { void FixupPbrTextureSettings(const TArray<UObject*>& Imported); }

FString FMeshyAssetImporter::ResolvePackagePath(const FString& Requested)
{
	FString Path = Requested.TrimStartAndEnd();

	if (Path.IsEmpty() || !UECPMountResolver::IsValidMountedPath(Path))
	{
		Path = DefaultPackagePath;
	}

	while (Path.EndsWith(TEXT("/")))
	{
		Path.LeftChopInline(1);
	}
	return Path;
}

FMeshyMeshImportResult FMeshyAssetImporter::ImportMeshFromFile(
	const FString& FilePath,
	const FString& AssetName,
	const FString& PackagePath)
{
	FMeshyMeshImportResult Result;

	if (FilePath.IsEmpty() || !FPaths::FileExists(FilePath))
	{
		Result.ErrorMessage = FString::Printf(TEXT("Source file missing: %s"), *FilePath);
		return Result;
	}

	const FString TargetDir = ResolvePackagePath(PackagePath);
	Result.PackagePath = TargetDir;

	const FString TargetAssetPath = FString::Printf(TEXT("%s/%s"), *TargetDir, *AssetName);
	if (UEditorAssetLibrary::DoesAssetExist(TargetAssetPath))
	{
		Result.ErrorMessage = FString::Printf(TEXT("Asset already exists: %s"), *TargetAssetPath);
		return Result;
	}

	if (!UEditorAssetLibrary::DoesDirectoryExist(TargetDir))
	{
		UEditorAssetLibrary::MakeDirectory(TargetDir);
	}

	FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools");

	auto RunSilentImport = [&AssetToolsModule, &FilePath](const FString& Dest) -> TArray<UObject*>
	{
		UAssetImportTask* Task = NewObject<UAssetImportTask>();
		Task->Filename         = FilePath;
		Task->DestinationPath  = Dest;
		Task->DestinationName  = FPaths::GetBaseFilename(FilePath);
		Task->bReplaceExisting = true;
		Task->bAutomated       = true;
		Task->bSave            = true;
		TArray<UAssetImportTask*> Tasks; Tasks.Add(Task);
		AssetToolsModule.Get().ImportAssetTasks(Tasks);
		return Task->GetObjects();
	};

	TArray<UObject*> Imported = RunSilentImport(TargetDir);

	FString FinalDir = TargetDir;
	if (Imported.Num() == 0 && !TargetDir.Equals(DefaultPackagePath))
	{
		FinalDir = DefaultPackagePath;
		if (!UEditorAssetLibrary::DoesDirectoryExist(FinalDir))
		{
			UEditorAssetLibrary::MakeDirectory(FinalDir);
		}
		Imported = RunSilentImport(FinalDir);
		Result.PackagePath = FinalDir;
	}

	if (Imported.Num() == 0)
	{
		Result.ErrorMessage = TEXT("AssetTools.ImportAssets returned no objects");
		return Result;
	}

	const FString ImportedBaseName = FPaths::GetBaseFilename(FilePath);
	TArray<FAssetRenameData> Renames;

	for (UObject* Obj : Imported)
	{
		if (!Obj) continue;

		const FString OldName = Obj->GetName();
		const FString OldPackage = FPackageName::GetLongPackagePath(Obj->GetOutermost()->GetName());

		if (UStaticMesh* Mesh = Cast<UStaticMesh>(Obj))
		{
			Mesh->MarkPackageDirty();
			Result.PrimaryAssetPath = FString::Printf(TEXT("%s/%s"), *FinalDir, *AssetName);
			Result.bSuccess = true;

			if (!AssetName.IsEmpty() && OldName != AssetName)
			{
				Renames.Add(FAssetRenameData(Mesh, OldPackage, AssetName));
			}
		}
		else if (!AssetName.IsEmpty() && !ImportedBaseName.IsEmpty() && OldName.StartsWith(ImportedBaseName))
		{
			const FString Suffix  = OldName.Mid(ImportedBaseName.Len());
			const FString NewName = AssetName + Suffix;
			if (OldName != NewName)
			{
				Renames.Add(FAssetRenameData(Obj, OldPackage, NewName));
			}
		}
	}

	if (Renames.Num() > 0)
	{
		AssetToolsModule.Get().RenameAssets(Renames);
	}

	FixupPbrTextureSettings(Imported);

	if (!Result.bSuccess && Result.ErrorMessage.IsEmpty())
	{
		Result.ErrorMessage = TEXT("Import produced no UStaticMesh");
	}

	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	AssetRegistryModule.Get().ScanPathsSynchronous({ FinalDir },  true);

	return Result;
}

FMeshyTextureImportResult FMeshyAssetImporter::ImportTextureFromFile(
	const FString& FilePath,
	const FString& AssetName,
	const FString& PackagePath,
	bool bSRGB,
	TextureCompressionSettings Compression)
{
	FMeshyTextureImportResult Result;

	TArray<uint8> ImageBytes;
	if (!FFileHelper::LoadFileToArray(ImageBytes, *FilePath))
	{
		Result.ErrorMessage = FString::Printf(TEXT("Failed to read texture bytes from %s"), *FilePath);
		return Result;
	}

	const FString TargetDir = ResolvePackagePath(PackagePath);
	if (!UEditorAssetLibrary::DoesDirectoryExist(TargetDir))
	{
		UEditorAssetLibrary::MakeDirectory(TargetDir);
	}

	const FString TargetAssetPath = FString::Printf(TEXT("%s/%s"), *TargetDir, *AssetName);
	if (UEditorAssetLibrary::DoesAssetExist(TargetAssetPath))
	{
		Result.ErrorMessage = FString::Printf(TEXT("Texture already exists: %s"), *TargetAssetPath);
		return Result;
	}

	UPackage* Package = CreatePackage(*TargetAssetPath);
	if (!Package)
	{
		Result.ErrorMessage = TEXT("CreatePackage returned null");
		return Result;
	}
	Package->FullyLoad();

	UTextureFactory* Factory = NewObject<UTextureFactory>();
	Factory->SuppressImportOverwriteDialog();

	const uint8* DataPtr = ImageBytes.GetData();
	const uint8* DataEnd = DataPtr + ImageBytes.Num();

	UObject* Created = Factory->FactoryCreateBinary(
		UTexture2D::StaticClass(), Package, FName(*AssetName),
		RF_Public | RF_Standalone, nullptr, TEXT("png"),
		DataPtr, DataEnd, GWarn);

	UTexture2D* Texture = Cast<UTexture2D>(Created);
	if (!Texture)
	{
		Result.ErrorMessage = TEXT("TextureFactory failed to produce a Texture2D");
		return Result;
	}

	Texture->SRGB                = bSRGB;
	Texture->CompressionSettings = Compression;
	Texture->UpdateResource();
	Texture->PostEditChange();
	Texture->MarkPackageDirty();

	FAssetRegistryModule::AssetCreated(Texture);
	Result.AssetPath = TargetAssetPath;
	Result.bSuccess  = true;
	return Result;
}

namespace
{
	TArray<UObject*> RunImportTask(
		const FString& SourcePath,
		const FString& DestPath,
		UFactory* Factory,
		FString& OutError)
	{
		UAssetImportTask* Task = NewObject<UAssetImportTask>();
		Task->Filename         = SourcePath;
		Task->DestinationPath  = DestPath;
		Task->DestinationName  = FPaths::GetBaseFilename(SourcePath);
		Task->bReplaceExisting = true;
		Task->bAutomated       = true;
		Task->bSave            = true;
		if (Factory) Task->Factory = Factory;

		FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools");
		TArray<UAssetImportTask*> Tasks; Tasks.Add(Task);
		AssetToolsModule.Get().ImportAssetTasks(Tasks);

		TArray<UObject*> All = Task->GetObjects();
		for (const FString& AssetPath : Task->ImportedObjectPaths)
		{
			if (AssetPath.IsEmpty()) continue;
			if (UObject* Obj = LoadObject<UObject>(nullptr, *AssetPath))
			{
				All.AddUnique(Obj);
			}
		}

		if (All.Num() == 0)
		{
			OutError = FString::Printf(TEXT("Import produced no objects: %s"), *SourcePath);
			return {};
		}
		return All;
	}

	void RenameImportedSubAssets(
		const TArray<UObject*>& Imported,
		const FString& ImportedBaseName,
		const FString& AssetName)
	{
		if (AssetName.IsEmpty() || ImportedBaseName.IsEmpty()) return;

		FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools");
		TArray<FAssetRenameData> Renames;

		for (UObject* Obj : Imported)
		{
			if (!Obj) continue;

			const FString OldName    = Obj->GetName();
			const FString OldPackage = FPackageName::GetLongPackagePath(Obj->GetOutermost()->GetName());

			FString NewName;
			if (OldName == ImportedBaseName)
			{
				NewName = AssetName;
			}
			else if (OldName.StartsWith(ImportedBaseName))
			{
				NewName = AssetName + OldName.Mid(ImportedBaseName.Len());
			}
			else
			{
				continue;
			}

			if (OldName != NewName)
			{
				Renames.Add(FAssetRenameData(Obj, OldPackage, NewName));
			}
		}

		if (Renames.Num() > 0)
		{
			AssetToolsModule.Get().RenameAssets(Renames);
		}
	}

	void FixupPbrTextureSettings(const TArray<UObject*>& Imported)
	{
		auto LowerContainsAny = [](const FString& Lower, std::initializer_list<const TCHAR*> Keys)
		{
			for (const TCHAR* K : Keys) if (Lower.Contains(K)) return true;
			return false;
		};

		for (UObject* Obj : Imported)
		{
			UTexture2D* Tex = Cast<UTexture2D>(Obj);
			if (!Tex) continue;

			const FString Name  = Tex->GetName();
			const FString Lower = Name.ToLower();

			const bool bIsNormal =
				LowerContainsAny(Lower, { TEXT("_normal"), TEXT("_nrm"), TEXT("normalmap") }) ||
				Lower.EndsWith(TEXT("_n"));

			const bool bIsLinearData = !bIsNormal && LowerContainsAny(Lower, {
				TEXT("_roughness"), TEXT("_metallic"), TEXT("_metalness"),
				TEXT("_ao"), TEXT("_occlusion"), TEXT("_orm"),
				TEXT("_metallicroughness"), TEXT("_occlusionroughnessmetallic"),
				TEXT("_mrao"), TEXT("_specular"),
			});

			bool bChanged = false;
			if (bIsNormal)
			{
				if (Tex->SRGB)                                            { Tex->SRGB = false;                                  bChanged = true; }
				if (Tex->CompressionSettings != TC_Normalmap)             { Tex->CompressionSettings = TC_Normalmap;            bChanged = true; }
				if (Tex->LODGroup != TEXTUREGROUP_WorldNormalMap)         { Tex->LODGroup = TEXTUREGROUP_WorldNormalMap;        bChanged = true; }
			}
			else if (bIsLinearData)
			{
				if (Tex->SRGB)                                            { Tex->SRGB = false;                                  bChanged = true; }
			}
			else
			{
				if (Tex->CompressionSettings == TC_Default)               { Tex->CompressionSettings = TC_BC7;                  bChanged = true; }
			}

			if (Tex->MipGenSettings != TMGS_NoMipmaps)
			{
				Tex->MipGenSettings = TMGS_NoMipmaps;
				bChanged = true;
			}

			if (bChanged)
			{
				Tex->UpdateResource();
				Tex->PostEditChange();
				Tex->MarkPackageDirty();
				UE_LOG(LogUECPAssetGen, Log,
					TEXT("Meshy: PBR fixup on %s (normal=%d linear=%d)"),
					*Name, bIsNormal ? 1 : 0, bIsLinearData ? 1 : 0);
			}
		}
	}
}

FMeshySkeletalImportResult FMeshyAssetImporter::ImportSkeletalMeshFromFile(
	const FString& FilePath,
	const FString& AssetName,
	const FString& PackagePath)
{
	FMeshySkeletalImportResult Result;

	if (FilePath.IsEmpty() || !FPaths::FileExists(FilePath))
	{
		Result.ErrorMessage = FString::Printf(TEXT("Source file missing: %s"), *FilePath);
		return Result;
	}

	const FString TargetDir = ResolvePackagePath(PackagePath);
	Result.PackagePath = TargetDir;
	if (!UEditorAssetLibrary::DoesDirectoryExist(TargetDir))
	{
		UEditorAssetLibrary::MakeDirectory(TargetDir);
	}

	UFbxFactory* Factory = NewObject<UFbxFactory>();
	Factory->ImportUI->bImportMesh         = true;
	Factory->ImportUI->bImportAnimations   = false;
	Factory->ImportUI->bImportMaterials    = true;
	Factory->ImportUI->bImportTextures     = true;
	Factory->ImportUI->MeshTypeToImport    = FBXIT_SkeletalMesh;
	Factory->ImportUI->bCreatePhysicsAsset = true;
	Factory->ImportUI->Skeleton            = nullptr;
	if (Factory->ImportUI->SkeletalMeshImportData)
	{
		Factory->ImportUI->SkeletalMeshImportData->NormalImportMethod      = FBXNIM_ImportNormalsAndTangents;
		Factory->ImportUI->SkeletalMeshImportData->NormalGenerationMethod  = EFBXNormalGenerationMethod::MikkTSpace;
	}

	const TArray<UObject*> Imported = RunImportTask(FilePath, TargetDir, Factory, Result.ErrorMessage);
	if (Imported.Num() == 0) return Result;

	const FString ImportedBaseName = FPaths::GetBaseFilename(FilePath);
	RenameImportedSubAssets(Imported, ImportedBaseName, AssetName);
	FixupPbrTextureSettings(Imported);

	for (UObject* Obj : Imported)
	{
		if (!Obj) continue;
		if (USkeletalMesh* Mesh = Cast<USkeletalMesh>(Obj))
		{
			Result.SkeletalMeshPath = Mesh->GetPathName();
			Result.bSuccess         = true;
			if (USkeleton* LinkedSkel = Mesh->GetSkeleton())
			{
				Result.SkeletonPath = LinkedSkel->GetPathName();
			}
			if (UPhysicsAsset* LinkedPA = Mesh->GetPhysicsAsset())
			{
				Result.PhysicsAssetPath = LinkedPA->GetPathName();
			}
		}
		else if (USkeleton* Skel = Cast<USkeleton>(Obj))
		{
			if (Result.SkeletonPath.IsEmpty()) Result.SkeletonPath = Skel->GetPathName();
		}
		else if (UPhysicsAsset* PA = Cast<UPhysicsAsset>(Obj))
		{
			if (Result.PhysicsAssetPath.IsEmpty()) Result.PhysicsAssetPath = PA->GetPathName();
		}
	}

	UE_LOG(LogUECPAssetGen, Log,
		TEXT("Meshy: imported SkeletalMesh=%s  Skeleton=%s  PhysicsAsset=%s"),
		*Result.SkeletalMeshPath, *Result.SkeletonPath, *Result.PhysicsAssetPath);

	if (!Result.bSuccess && Result.ErrorMessage.IsEmpty())
	{
		Result.ErrorMessage = TEXT("Import produced no USkeletalMesh");
	}

	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	AssetRegistryModule.Get().ScanPathsSynchronous({ TargetDir },  true);

	return Result;
}

FMeshyAnimationImportResult FMeshyAssetImporter::ImportAnimationFromFile(
	const FString& FilePath,
	const FString& AssetName,
	const FString& PackagePath,
	const FString& SkeletonAssetPath)
{
	FMeshyAnimationImportResult Result;

	if (FilePath.IsEmpty() || !FPaths::FileExists(FilePath))
	{
		Result.ErrorMessage = FString::Printf(TEXT("Source file missing: %s"), *FilePath);
		return Result;
	}

	if (SkeletonAssetPath.IsEmpty())
	{
		Result.ErrorMessage = TEXT("SkeletonAssetPath is required for animation import");
		return Result;
	}

	USkeleton* Skeleton = Cast<USkeleton>(UEditorAssetLibrary::LoadAsset(SkeletonAssetPath));
	if (!Skeleton)
	{
		Result.ErrorMessage = FString::Printf(TEXT("Skeleton not found at %s"), *SkeletonAssetPath);
		return Result;
	}

	const FString TargetDir = ResolvePackagePath(PackagePath);
	if (!UEditorAssetLibrary::DoesDirectoryExist(TargetDir))
	{
		UEditorAssetLibrary::MakeDirectory(TargetDir);
	}

	UFbxFactory* Factory = NewObject<UFbxFactory>();
	Factory->ImportUI->bImportMesh       = false;
	Factory->ImportUI->bImportAnimations = true;
	Factory->ImportUI->bImportMaterials  = false;
	Factory->ImportUI->bImportTextures   = false;
	Factory->ImportUI->MeshTypeToImport  = FBXIT_Animation;
	Factory->ImportUI->Skeleton          = Skeleton;

	const TArray<UObject*> Imported = RunImportTask(FilePath, TargetDir, Factory, Result.ErrorMessage);
	if (Imported.Num() == 0) return Result;

	const FString ImportedBaseName = FPaths::GetBaseFilename(FilePath);
	RenameImportedSubAssets(Imported, ImportedBaseName, AssetName);

	for (UObject* Obj : Imported)
	{
		if (UAnimSequence* Anim = Cast<UAnimSequence>(Obj))
		{
			Result.AnimSequencePath = Anim->GetPathName();
			Result.bSuccess         = true;
			break;
		}
	}

	if (!Result.bSuccess && Result.ErrorMessage.IsEmpty())
	{
		Result.ErrorMessage = TEXT("Import produced no UAnimSequence");
	}

	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	AssetRegistryModule.Get().ScanPathsSynchronous({ TargetDir },  true);

	return Result;
}

FString FMeshyAssetImporter::ExportStaticMeshToFbxTemp(const FString& MeshAssetPath, FString& OutError)
{
	UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, *MeshAssetPath);
	if (!Mesh)
	{
		OutError = FString::Printf(TEXT("Failed to load StaticMesh at %s"), *MeshAssetPath);
		return FString();
	}

	UExporter* Exporter = nullptr;
	for (TObjectIterator<UClass> It; It; ++It)
	{
		if (!It->IsChildOf(UExporter::StaticClass()) || It->HasAnyClassFlags(CLASS_Abstract)) continue;
		UExporter* Test = Cast<UExporter>(It->GetDefaultObject());
		if (!Test || Test->SupportedClass != UStaticMesh::StaticClass()) continue;
		for (const FString& Ext : Test->FormatExtension)
		{
			if (Ext.ToLower() == TEXT("fbx"))
			{
				Exporter = NewObject<UExporter>(GetTransientPackage(), *It);
				break;
			}
		}
		if (Exporter) break;
	}
	if (!Exporter)
	{
		OutError = TEXT("No FBX exporter registered for UStaticMesh");
		return FString();
	}

	const FString TempPath = FPaths::ProjectSavedDir() / TEXT("Temp") /
		FString::Printf(TEXT("meshy_input_%s.fbx"), *FGuid::NewGuid().ToString(EGuidFormats::Digits).Left(8));
	IFileManager::Get().MakeDirectory(*FPaths::GetPath(TempPath),  true);

	UAssetExportTask* Task = NewObject<UAssetExportTask>();
	Task->Object             = Mesh;
	Task->Exporter           = Exporter;
	Task->Filename           = TempPath;
	Task->bSelected          = false;
	Task->bReplaceIdentical  = true;
	Task->bPrompt            = false;
	Task->bAutomated         = true;
	Task->bUseFileArchive    = false;
	Task->bWriteEmptyFiles   = false;
	if (!UExporter::RunAssetExportTask(Task) || !FPaths::FileExists(TempPath))
	{
		OutError = FString::Printf(TEXT("FBX export failed for %s"), *MeshAssetPath);
		return FString();
	}
	const int64 Bytes = IFileManager::Get().FileSize(*TempPath);
	UE_LOG(LogUECPAssetGen, Log, TEXT("Meshy: exported %s → %s (%lld bytes)"), *MeshAssetPath, *TempPath, Bytes);
	return TempPath;
}

FString FMeshyAssetImporter::FileToBase64DataUri(const FString& FilePath, FString& OutError)
{
	TArray<uint8> Bytes;
	if (!FFileHelper::LoadFileToArray(Bytes, *FilePath))
	{
		OutError = FString::Printf(TEXT("Failed to read %s"), *FilePath);
		return FString();
	}
	return TEXT("data:application/octet-stream;base64,") + FBase64::Encode(Bytes);
}
