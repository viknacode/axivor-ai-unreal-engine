// Copyright 2026, BlueprintsLab, All rights reserved.

#include "Tools/ImportTools.h"
#include "Misc/PackageName.h"
#include "Factories/Factory.h"

#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "AssetImportTask.h"
#include "Factories/TextureFactory.h"
#include "Factories/FbxFactory.h"
#include "Factories/FbxImportUI.h"
#include "Factories/FbxStaticMeshImportData.h"
#include "Factories/FbxSkeletalMeshImportData.h"
#include "Factories/FbxAnimSequenceImportData.h"
#include "Factories/SoundFactory.h"
#include "EditorAssetLibrary.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "Engine/Texture2D.h"
#include "Engine/StaticMesh.h"
#include "Engine/SkeletalMesh.h"
#include "Sound/SoundWave.h"
#include "Animation/AnimSequence.h"
#include "Animation/Skeleton.h"
#include "HAL/FileManager.h"

namespace ImportTools
{

static UObject* RunImportTask(const FString& SourcePath, const FString& DestPath,
	UFactory* Factory, FString& OutError)
{
	if (!IFileManager::Get().FileExists(*SourcePath))
	{
		OutError = FString::Printf(TEXT("Source file not found: %s"), *SourcePath);
		return nullptr;
	}

	UAssetImportTask* Task = NewObject<UAssetImportTask>();
	Task->Filename = SourcePath;
	Task->DestinationPath = DestPath;
	Task->DestinationName = FPaths::GetBaseFilename(SourcePath);
	Task->bReplaceExisting = true;
	Task->bAutomated = true;
	Task->bSave = true;
	if (Factory) Task->Factory = Factory;

	FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools"));
	TArray<UAssetImportTask*> Tasks;
	Tasks.Add(Task);
	AssetToolsModule.Get().ImportAssetTasks(Tasks);

	if (Task->GetObjects().Num() > 0)
	{
		return Task->GetObjects()[0];
	}

	// Axivor: give the model something actionable instead of "check the Output Log".
	{
		const FString Ext = FPaths::GetExtension(SourcePath).ToLower();
		FString Reason;
		if (!FPackageName::IsValidLongPackageName(DestPath / TEXT("X"), /*bIncludeReadOnlyRoots*/ true))
			Reason = FString::Printf(TEXT("destination '%s' is not a valid content path (use /Game/...)"), *DestPath);
		else if (Factory && !Factory->FactoryCanImport(SourcePath))
			Reason = FString::Printf(TEXT("factory %s does not accept '.%s' files"), *Factory->GetClass()->GetName(), *Ext);
		else if (!Factory && Ext.IsEmpty())
			Reason = TEXT("file has no extension so no importer could be selected");
		else
			Reason = TEXT("the importer produced no asset (unsupported format, corrupt file, or the import dialog was suppressed by bAutomated)");
		OutError = FString::Printf(TEXT("Import failed for '%s' → '%s': %s. Formats: fbx/obj/gltf (meshes+anims), png/jpg/tga/exr/hdr (textures), wav/ogg (audio), csv/json (data tables)."),
			*SourcePath, *DestPath, *Reason);
	}
	return nullptr;
}

static FString MakeSuccessJson(UObject* Asset, const FString& AssetType)
{
	TSharedPtr<FJsonObject> Res = MakeShareable(new FJsonObject());
	Res->SetBoolField(TEXT("success"), true);
	Res->SetStringField(TEXT("asset_path"), Asset->GetPathName());
	Res->SetStringField(TEXT("asset_name"), Asset->GetName());
	Res->SetStringField(TEXT("asset_type"), AssetType);
	Res->SetStringField(TEXT("class"), Asset->GetClass()->GetName());

	FString OutStr;
	TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&OutStr);
	FJsonSerializer::Serialize(Res.ToSharedRef(), W);
	return OutStr;
}

void HandleImportTexture(const FString& FilePath, const FString& DestinationPath,
	bool bSRGB, const FString& CompressionSettings, const FString& LODGroup,
	FString& OutJsonString, FString& OutError)
{

	if (FilePath.IsEmpty()) { OutError = TEXT("file_path is required"); return; }

	FString DestPath = DestinationPath.IsEmpty() ? TEXT("/Game/Textures") : DestinationPath;

	UTextureFactory* Factory = NewObject<UTextureFactory>();
	Factory->bUseHashAsGuid = true;
	Factory->SuppressImportOverwriteDialog();

	UObject* Imported = RunImportTask(FilePath, DestPath, Factory, OutError);
	if (!Imported) return;

	UTexture2D* Tex = Cast<UTexture2D>(Imported);
	if (Tex)
	{
		Tex->SRGB = bSRGB;

		if (!CompressionSettings.IsEmpty())
		{
			FString CS = CompressionSettings.ToLower();
			if (CS == TEXT("default")) Tex->CompressionSettings = TC_Default;
			else if (CS == TEXT("normalmap")) Tex->CompressionSettings = TC_Normalmap;
			else if (CS == TEXT("masks") || CS == TEXT("linearnormals")) Tex->CompressionSettings = TC_Masks;
			else if (CS == TEXT("grayscale")) Tex->CompressionSettings = TC_Grayscale;
			else if (CS == TEXT("displacementmap")) Tex->CompressionSettings = TC_Displacementmap;
			else if (CS == TEXT("vectordisplacementmap")) Tex->CompressionSettings = TC_VectorDisplacementmap;
			else if (CS == TEXT("hdr")) Tex->CompressionSettings = TC_HDR;
			else if (CS == TEXT("alpha")) Tex->CompressionSettings = TC_Alpha;
		}

		if (!LODGroup.IsEmpty())
		{
			FString LG = LODGroup.ToLower();
			if (LG == TEXT("world")) Tex->LODGroup = TEXTUREGROUP_World;
			else if (LG == TEXT("character")) Tex->LODGroup = TEXTUREGROUP_Character;
			else if (LG == TEXT("ui")) Tex->LODGroup = TEXTUREGROUP_UI;
			else if (LG == TEXT("effects")) Tex->LODGroup = TEXTUREGROUP_Effects;
		}

		Tex->UpdateResource();
		Tex->MarkPackageDirty();
	}

	OutJsonString = MakeSuccessJson(Imported, TEXT("Texture"));
}

void HandleImportStaticMesh(const FString& FilePath, const FString& DestinationPath,
	float ImportScale, bool bCombineMeshes, bool bGenerateCollision, bool bAutoComputeLOD,
	FString& OutJsonString, FString& OutError)
{

	if (FilePath.IsEmpty()) { OutError = TEXT("file_path is required"); return; }

	FString DestPath = DestinationPath.IsEmpty() ? TEXT("/Game/Meshes") : DestinationPath;

	UFbxFactory* Factory = NewObject<UFbxFactory>();
	Factory->ImportUI->bImportMesh = true;
	Factory->ImportUI->bImportAnimations = false;
	Factory->ImportUI->bImportMaterials = true;
	Factory->ImportUI->bImportTextures = true;
	Factory->ImportUI->MeshTypeToImport = FBXIT_StaticMesh;

	if (ImportScale != 1.0f && ImportScale > 0.f)
	{
		Factory->ImportUI->StaticMeshImportData->ImportUniformScale = ImportScale;
	}

	Factory->ImportUI->StaticMeshImportData->bCombineMeshes = bCombineMeshes;
	Factory->ImportUI->StaticMeshImportData->bGenerateLightmapUVs = true;

	UObject* Imported = RunImportTask(FilePath, DestPath, Factory, OutError);
	if (!Imported) return;

	UStaticMesh* SM = Cast<UStaticMesh>(Imported);
	if (SM && bGenerateCollision)
	{
		SM->MarkPackageDirty();
	}

	OutJsonString = MakeSuccessJson(Imported, TEXT("StaticMesh"));
}

void HandleImportSkeletalMesh(const FString& FilePath, const FString& DestinationPath,
	const FString& SkeletonPath, float ImportScale,
	FString& OutJsonString, FString& OutError)
{

	if (FilePath.IsEmpty()) { OutError = TEXT("file_path is required"); return; }

	FString DestPath = DestinationPath.IsEmpty() ? TEXT("/Game/Meshes") : DestinationPath;

	UFbxFactory* Factory = NewObject<UFbxFactory>();
	Factory->ImportUI->bImportMesh = true;
	Factory->ImportUI->bImportAnimations = false;
	Factory->ImportUI->bImportMaterials = true;
	Factory->ImportUI->bImportTextures = true;
	Factory->ImportUI->MeshTypeToImport = FBXIT_SkeletalMesh;

	if (ImportScale != 1.0f && ImportScale > 0.f)
	{
		Factory->ImportUI->SkeletalMeshImportData->ImportUniformScale = ImportScale;
	}

	if (!SkeletonPath.IsEmpty())
	{
		USkeleton* Skeleton = Cast<USkeleton>(UEditorAssetLibrary::LoadAsset(SkeletonPath));
		if (Skeleton)
		{
			Factory->ImportUI->Skeleton = Skeleton;
		}
	}

	UObject* Imported = RunImportTask(FilePath, DestPath, Factory, OutError);
	if (!Imported) return;

	OutJsonString = MakeSuccessJson(Imported, TEXT("SkeletalMesh"));
}

void HandleImportSoundWave(const FString& FilePath, const FString& DestinationPath,
	FString& OutJsonString, FString& OutError)
{

	if (FilePath.IsEmpty()) { OutError = TEXT("file_path is required"); return; }

	FString DestPath = DestinationPath.IsEmpty() ? TEXT("/Game/Audio") : DestinationPath;

	UObject* Imported = RunImportTask(FilePath, DestPath, nullptr, OutError);
	if (!Imported) return;

	OutJsonString = MakeSuccessJson(Imported, TEXT("SoundWave"));
}

void HandleImportAnimation(const FString& FilePath, const FString& DestinationPath,
	const FString& SkeletonPath, FString& OutJsonString, FString& OutError)
{

	if (FilePath.IsEmpty()) { OutError = TEXT("file_path is required"); return; }
	if (SkeletonPath.IsEmpty()) { OutError = TEXT("skeleton_path is required for animation import"); return; }

	USkeleton* Skeleton = Cast<USkeleton>(UEditorAssetLibrary::LoadAsset(SkeletonPath));
	if (!Skeleton)
	{
		OutError = FString::Printf(TEXT("Skeleton not found at '%s'"), *SkeletonPath);
		return;
	}

	FString DestPath = DestinationPath.IsEmpty() ? TEXT("/Game/Animations") : DestinationPath;

	UFbxFactory* Factory = NewObject<UFbxFactory>();
	Factory->ImportUI->bImportMesh = false;
	Factory->ImportUI->bImportAnimations = true;
	Factory->ImportUI->bImportMaterials = false;
	Factory->ImportUI->bImportTextures = false;
	Factory->ImportUI->MeshTypeToImport = FBXIT_Animation;
	Factory->ImportUI->Skeleton = Skeleton;

	UObject* Imported = RunImportTask(FilePath, DestPath, Factory, OutError);
	if (!Imported) return;

	OutJsonString = MakeSuccessJson(Imported, TEXT("AnimSequence"));
}

void HandleImportAsset(const FString& FilePath, const FString& DestinationPath,
	FString& OutJsonString, FString& OutError)
{

	if (FilePath.IsEmpty()) { OutError = TEXT("file_path is required"); return; }

	FString Ext = FPaths::GetExtension(FilePath).ToLower();

	if (Ext == TEXT("png") || Ext == TEXT("tga") || Ext == TEXT("exr") || Ext == TEXT("jpg") ||
		Ext == TEXT("jpeg") || Ext == TEXT("bmp") || Ext == TEXT("hdr"))
	{
		HandleImportTexture(FilePath, DestinationPath, true, TEXT(""), TEXT(""), OutJsonString, OutError);
	}
	else if (Ext == TEXT("wav") || Ext == TEXT("ogg") || Ext == TEXT("flac"))
	{
		HandleImportSoundWave(FilePath, DestinationPath, OutJsonString, OutError);
	}
	else if (Ext == TEXT("fbx") || Ext == TEXT("obj") || Ext == TEXT("gltf") || Ext == TEXT("glb"))
	{
		HandleImportStaticMesh(FilePath, DestinationPath, 1.0f, true, true, false, OutJsonString, OutError);
	}
	else
	{
		FString DestPath = DestinationPath.IsEmpty() ? TEXT("/Game") : DestinationPath;
		UObject* Imported = RunImportTask(FilePath, DestPath, nullptr, OutError);
		if (!Imported) return;
		OutJsonString = MakeSuccessJson(Imported, Imported->GetClass()->GetName());
	}
}

void HandleImportTextureFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString FilePath, DestPath, CompressionSettings, LODGroup;
	bool bSRGB = true;
	Args->TryGetStringField(TEXT("file_path"), FilePath);
	Args->TryGetStringField(TEXT("destination_path"), DestPath);
	Args->TryGetBoolField(TEXT("srgb"), bSRGB);
	Args->TryGetStringField(TEXT("compression"), CompressionSettings);
	Args->TryGetStringField(TEXT("lod_group"), LODGroup);
	HandleImportTexture(FilePath, DestPath, bSRGB, CompressionSettings, LODGroup, OutJsonString, OutError);
}

void HandleImportStaticMeshFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString FilePath, DestPath;
	double ImportScale = 1.0;
	bool bCombineMeshes = true, bGenerateCollision = true, bAutoComputeLOD = false;
	Args->TryGetStringField(TEXT("file_path"), FilePath);
	Args->TryGetStringField(TEXT("destination_path"), DestPath);
	Args->TryGetNumberField(TEXT("import_scale"), ImportScale);
	Args->TryGetBoolField(TEXT("combine_meshes"), bCombineMeshes);
	Args->TryGetBoolField(TEXT("generate_collision"), bGenerateCollision);
	Args->TryGetBoolField(TEXT("auto_compute_lod"), bAutoComputeLOD);
	HandleImportStaticMesh(FilePath, DestPath, (float)ImportScale,
		bCombineMeshes, bGenerateCollision, bAutoComputeLOD, OutJsonString, OutError);
}

void HandleImportSkeletalMeshFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString FilePath, DestPath, SkeletonPath;
	double ImportScale = 1.0;
	Args->TryGetStringField(TEXT("file_path"), FilePath);
	Args->TryGetStringField(TEXT("destination_path"), DestPath);
	Args->TryGetStringField(TEXT("skeleton_path"), SkeletonPath);
	Args->TryGetNumberField(TEXT("import_scale"), ImportScale);
	HandleImportSkeletalMesh(FilePath, DestPath, SkeletonPath, (float)ImportScale, OutJsonString, OutError);
}

void HandleImportSoundWaveFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString FilePath, DestPath;
	Args->TryGetStringField(TEXT("file_path"), FilePath);
	Args->TryGetStringField(TEXT("destination_path"), DestPath);
	HandleImportSoundWave(FilePath, DestPath, OutJsonString, OutError);
}

void HandleImportAnimationFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString FilePath, DestPath, SkeletonPath;
	Args->TryGetStringField(TEXT("file_path"), FilePath);
	Args->TryGetStringField(TEXT("destination_path"), DestPath);
	Args->TryGetStringField(TEXT("skeleton_path"), SkeletonPath);
	HandleImportAnimation(FilePath, DestPath, SkeletonPath, OutJsonString, OutError);
}

void HandleImportAssetFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString FilePath, DestPath;
	Args->TryGetStringField(TEXT("file_path"), FilePath);
	Args->TryGetStringField(TEXT("destination_path"), DestPath);
	HandleImportAsset(FilePath, DestPath, OutJsonString, OutError);
}

}
