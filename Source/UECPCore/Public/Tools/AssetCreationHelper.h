// Copyright 2026, BlueprintsLab, All rights reserved

#pragma once

#include "CoreMinimal.h"
#include "AssetToolsModule.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Modules/ModuleManager.h"
#include "Misc/PackageName.h"

namespace AssetCreationHelper
{

	inline bool DoesAssetExist(const FString& Name, const FString& PackagePath,
		FString* OutExistingClass = nullptr, FString* OutExistingPath = nullptr)
	{
		if (Name.IsEmpty() || PackagePath.IsEmpty()) return false;

		FString Pkg = PackagePath;
		while (Pkg.EndsWith(TEXT("/"))) Pkg = Pkg.LeftChop(1);

		const FString ObjectPath = FString::Printf(TEXT("%s/%s.%s"), *Pkg, *Name, *Name);

		const FAssetRegistryModule& AssetRegistryModule =
			FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
		const FAssetData ExistingAsset =
			AssetRegistryModule.Get().GetAssetByObjectPath(FSoftObjectPath(ObjectPath));

		if (!ExistingAsset.IsValid()) return false;

		if (OutExistingClass)
			*OutExistingClass = ExistingAsset.AssetClassPath.GetAssetName().ToString();
		if (OutExistingPath)
			*OutExistingPath = ObjectPath;
		return true;
	}

	inline bool BailIfDifferentClassExists(const FString& Name, const FString& PackagePath,
		const FString& ExpectedClassName, FString& OutJsonString, FString& OutError)
	{
		FString ExistingClass, ExistingPath;
		if (!DoesAssetExist(Name, PackagePath, &ExistingClass, &ExistingPath)) return false;
		if (ExistingClass.Equals(ExpectedClassName, ESearchCase::IgnoreCase)) return false;

		OutError = FString::Printf(
			TEXT("An asset of a different class already exists at this path. Existing: '%s' (class: %s). Requested class: %s. Pick a different name, or delete the existing asset first via asset_management(action='delete_asset', asset_path='%s')."),
			*ExistingPath, *ExistingClass, *ExpectedClassName, *ExistingPath);

		TSharedPtr<FJsonObject> Resp = MakeShareable(new FJsonObject);
		Resp->SetBoolField(TEXT("success"), false);
		Resp->SetStringField(TEXT("error"), OutError);
		Resp->SetStringField(TEXT("existing_path"), ExistingPath);
		Resp->SetStringField(TEXT("existing_class"), ExistingClass);
		Resp->SetStringField(TEXT("requested_class"), ExpectedClassName);
		FString S; TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&S);
		FJsonSerializer::Serialize(Resp.ToSharedRef(), W);
		OutJsonString = S;
		return true;
	}

	inline UObject* ReturnExistingIfSameClass(const FString& Name, const FString& PackagePath,
		const FString& ExpectedClassName, FString& OutJsonString)
	{
		FString ExistingClass, ExistingPath;
		if (!DoesAssetExist(Name, PackagePath, &ExistingClass, &ExistingPath)) return nullptr;
		if (!ExistingClass.Equals(ExpectedClassName, ESearchCase::IgnoreCase)) return nullptr;

		UObject* Existing = StaticLoadObject(UObject::StaticClass(), nullptr, *ExistingPath);
		if (!Existing) return nullptr;

		TSharedPtr<FJsonObject> Resp = MakeShareable(new FJsonObject);
		Resp->SetBoolField(TEXT("success"), true);
		Resp->SetBoolField(TEXT("existed"), true);
		Resp->SetStringField(TEXT("asset_path"), ExistingPath);
		Resp->SetStringField(TEXT("asset_class"), ExistingClass);
		Resp->SetStringField(TEXT("name"), Name);
		Resp->SetStringField(TEXT("note"), TEXT("Asset already existed at this path with the requested class — returned as-is. No 'overwrite?' dialog was shown. To force a fresh asset, delete the existing one first via asset_management(action='delete_asset')."));
		FString S; TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&S);
		FJsonSerializer::Serialize(Resp.ToSharedRef(), W);
		OutJsonString = S;
		return Existing;
	}
}
