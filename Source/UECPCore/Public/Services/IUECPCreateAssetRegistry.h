// Copyright 2026, BlueprintsLab, All rights reserved

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

struct FUECPCreateAssetResult
{
	bool    bSuccess = false;
	FString AssetPath;
	FString AssetClass;
	FString Error;
	TSharedPtr<FJsonObject> ExtraData;
};

class UECPCORE_API IUECPCreateAssetRegistry
{
public:
	virtual ~IUECPCreateAssetRegistry() = default;

	using FFactoryFn = TFunction<FUECPCreateAssetResult(
		const FString& Name,
		const FString& SavePath,
		const TSharedPtr<FJsonObject>& Options,
		FString& OutError)>;

	virtual void RegisterType(FName AssetType, FFactoryFn Factory, FName OwningExtension) = 0;

	virtual void UnregisterType(FName AssetType) = 0;

	virtual FUECPCreateAssetResult Create(
		const FString& AssetType,
		const FString& Name,
		const FString& SavePath,
		const TSharedPtr<FJsonObject>& Options,
		FString& OutError) = 0;

	virtual TArray<FName> ListTypes() const = 0;

	virtual FName GetOwningExtension(FName AssetType) const = 0;
};

namespace UECPCreateAsset
{

	using FFromArgsFn = void(*)(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError);

	inline IUECPCreateAssetRegistry::FFactoryFn FactoryFromArgsFn(FFromArgsFn Fn, const FString& AssetClass)
	{
		return [Fn, AssetClass](const FString& Name,
		                       const FString& SavePath,
		                       const TSharedPtr<FJsonObject>& Options,
		                       FString& OutError) -> FUECPCreateAssetResult
		{
			TSharedPtr<FJsonObject> Merged = MakeShared<FJsonObject>();
			if (Options.IsValid())
			{
				for (const auto& KV : Options->Values) Merged->SetField(KV.Key, KV.Value);
			}
			if (!Name.IsEmpty())     Merged->SetStringField(TEXT("name"),      Name);
			if (!SavePath.IsEmpty()) Merged->SetStringField(TEXT("save_path"), SavePath);

			FString OutJson;
			Fn(Merged, OutJson, OutError);

			FUECPCreateAssetResult Result;
			Result.bSuccess  = OutError.IsEmpty();
			Result.Error     = OutError;
			Result.AssetClass = AssetClass;

			if (!OutJson.IsEmpty())
			{
				TSharedPtr<FJsonObject> Parsed;
				TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(OutJson);
				if (FJsonSerializer::Deserialize(Reader, Parsed) && Parsed.IsValid())
				{
					static const TCHAR* PathKeys[] = {
						TEXT("asset_path"),
						TEXT("blueprint_path"),
						TEXT("material_path"),
						TEXT("system_path"),
						TEXT("data_table_path"),
						TEXT("anim_blueprint_path"),
						TEXT("path"),
					};
					for (const TCHAR* Key : PathKeys)
					{
						FString P;
						if (Parsed->TryGetStringField(Key, P) && !P.IsEmpty())
						{
							Result.AssetPath = MoveTemp(P);
							break;
						}
					}
					Result.ExtraData = Parsed;
				}
			}
			return Result;
		};
	}
}
