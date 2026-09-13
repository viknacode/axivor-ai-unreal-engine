// Copyright 2026, BlueprintsLab, All rights reserved

#include "Tools/AssetVerificationTools.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "EditorAssetLibrary.h"
#include "Utils/MountResolver.h"
#include "Engine/Blueprint.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstance.h"
#include "Sound/SoundCue.h"
#include "Sound/SoundNode.h"
#include "Sound/SoundNodeRandom.h"
#include "Sound/SoundNodeMixer.h"
#include "Sound/SoundNodeConcatenator.h"
#include "EdGraph/EdGraph.h"
#include "Modules/ModuleManager.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace AssetVerificationTools
{

namespace
{
	bool CueGraphHasEmptyContainers(USoundCue* Cue, FString& OutFirstOffender)
	{
		if (!Cue || !Cue->FirstNode) return false;
		TSet<USoundNode*> Visited;
		TArray<USoundNode*> Stack;
		Stack.Add(Cue->FirstNode);
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
			{
				OutFirstOffender = N->GetClass()->GetName();
				return true;
			}
			for (USoundNode* Child : N->ChildNodes)
				if (Child) Stack.Add(Child);
		}
		return false;
	}

	void AddIssue(TArray<TSharedPtr<FJsonValue>>& Issues, const FString& Path,
		const FString& Cls, const FString& Code, const FString& Message)
	{
		TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
		Obj->SetStringField(TEXT("asset_path"), Path);
		Obj->SetStringField(TEXT("asset_class"), Cls);
		Obj->SetStringField(TEXT("issue_code"), Code);
		Obj->SetStringField(TEXT("message"), Message);
		Issues.Add(MakeShareable(new FJsonValueObject(Obj)));
	}
}

void HandleVerifyAssetsInFolderFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }

	FString Folder;
	for (const TCHAR* Key : { TEXT("folder"), TEXT("path"), TEXT("folder_path"),
	                          TEXT("asset_path"), TEXT("system_path"), TEXT("dir") })
	{
		if (Args->TryGetStringField(Key, Folder) && !Folder.IsEmpty()) break;
	}
	if (Folder.IsEmpty()) { OutError = TEXT("Missing folder. Pass folder=/Game/Foo/Bar or /<PluginName>/Foo/Bar."); return; }
	if (!UECPMountResolver::IsValidMountedPath(Folder))
	{
		OutError = FString::Printf(TEXT("Folder '%s' is not under a known content mount. Use /Game/... or /<PluginName>/...."), *Folder);
		return;
	}

	bool bRecursive = true;
	Args->TryGetBoolField(TEXT("recursive"), bRecursive);

	const FAssetRegistryModule& ARM =
		FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
	TArray<FAssetData> Assets;
	ARM.Get().GetAssetsByPath(FName(*Folder), Assets, bRecursive,  false);

	int32 Healthy = 0;
	TArray<TSharedPtr<FJsonValue>> Issues;

	for (const FAssetData& AD : Assets)
	{
		const FString Path = AD.GetSoftObjectPath().ToString();
		const FString ClassName = AD.AssetClassPath.GetAssetName().ToString();

		UObject* Obj = AD.GetAsset();
		if (!Obj)
		{
			AddIssue(Issues, Path, ClassName, TEXT("failed_to_load"),
				TEXT("Asset registered but failed to load — possibly corrupt or missing source dependency."));
			continue;
		}

		bool bAssetOk = true;

		if (USoundCue* Cue = Cast<USoundCue>(Obj))
		{
			FString Offender;
			if (CueGraphHasEmptyContainers(Cue, Offender))
			{
				AddIssue(Issues, Path, ClassName, TEXT("sound_cue_empty_container"),
					FString::Printf(TEXT("SoundCue's reachable graph has a %s with 0 children — would crash on audio tick."), *Offender));
				bAssetOk = false;
			}
			if (Cue->FirstNode == nullptr)
			{
				AddIssue(Issues, Path, ClassName, TEXT("sound_cue_no_output"),
					TEXT("SoundCue has no FirstNode — silent on play."));
				bAssetOk = false;
			}
		}
		else if (UBlueprint* BP = Cast<UBlueprint>(Obj))
		{
			if (BP->Status == BS_Error)
			{
				AddIssue(Issues, Path, ClassName, TEXT("blueprint_compile_error"),
					TEXT("Blueprint compile status is BS_Error. Open in editor to inspect."));
				bAssetOk = false;
			}
			if (!BP->GeneratedClass)
			{
				AddIssue(Issues, Path, ClassName, TEXT("blueprint_no_generated_class"),
					TEXT("Blueprint has no GeneratedClass — likely never compiled successfully."));
				bAssetOk = false;
			}
		}
		else if (UMaterial* Mat = Cast<UMaterial>(Obj))
		{
			if (!Mat->GetEditorOnlyData())
			{
				AddIssue(Issues, Path, ClassName, TEXT("material_no_editor_data"),
					TEXT("Material has no EditorOnlyData — missing graph definition."));
				bAssetOk = false;
			}
		}
		else if (UMaterialInstance* MI = Cast<UMaterialInstance>(Obj))
		{
			if (!MI->Parent)
			{
				AddIssue(Issues, Path, ClassName, TEXT("material_instance_no_parent"),
					TEXT("MaterialInstance has no Parent — won't render anything."));
				bAssetOk = false;
			}
		}

		if (bAssetOk) ++Healthy;
	}

	TSharedPtr<FJsonObject> Resp = MakeShared<FJsonObject>();
	Resp->SetBoolField(TEXT("success"), true);
	Resp->SetStringField(TEXT("folder"), Folder);
	Resp->SetNumberField(TEXT("scanned"), Assets.Num());
	Resp->SetNumberField(TEXT("healthy"), Healthy);
	Resp->SetNumberField(TEXT("issue_count"), Issues.Num());
	Resp->SetArrayField(TEXT("issues"), Issues);

	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutJsonString);
	FJsonSerializer::Serialize(Resp.ToSharedRef(), Writer);
}

}
