// Copyright 2026, BlueprintsLab, All rights reserved

#include "Tools/CreateAssetTools.h"

#include "MCPToolsLog.h"
#include "Services/IUECPCreateAssetRegistry.h"
#include "Tools/BatchToolHelper.h"
#include "UECPCoreModule.h"
#include "Utils/MountResolver.h"

#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace CreateAssetTools
{
namespace
{
	FString SerialiseResult(const FUECPCreateAssetResult& Result)
	{
		TSharedPtr<FJsonObject> Out = MakeShared<FJsonObject>();
		Out->SetBoolField(TEXT("success"), Result.bSuccess);
		if (!Result.AssetPath.IsEmpty())  Out->SetStringField(TEXT("asset_path"),  Result.AssetPath);
		if (!Result.AssetClass.IsEmpty()) Out->SetStringField(TEXT("asset_class"), Result.AssetClass);
		if (!Result.Error.IsEmpty())      Out->SetStringField(TEXT("error"),      Result.Error);
		if (Result.ExtraData.IsValid())
		{
			Out->SetObjectField(TEXT("extra"), Result.ExtraData);
		}
		FString S;
		TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
			TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&S);
		FJsonSerializer::Serialize(Out.ToSharedRef(), Writer);
		return S;
	}

	FString BuildTypeListing()
	{
		if (!IUECPCoreModule::IsAvailable())
		{
			return TEXT("{\"success\":false,\"error\":\"UECPCore not available\"}");
		}
		IUECPCreateAssetRegistry& Reg = IUECPCoreModule::Get().GetCreateAssetRegistry();
		const TArray<FName> Types = Reg.ListTypes();

		TSharedPtr<FJsonObject> Out = MakeShared<FJsonObject>();
		Out->SetBoolField(TEXT("success"), true);
		Out->SetNumberField(TEXT("count"), Types.Num());

		TArray<TSharedPtr<FJsonValue>> Arr;
		Arr.Reserve(Types.Num());
		for (FName T : Types)
		{
			TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
			Entry->SetStringField(TEXT("asset_type"), T.ToString());
			const FName Owner = Reg.GetOwningExtension(T);
			Entry->SetStringField(TEXT("owning_extension"),
				Owner.IsNone() ? TEXT("<core>") : Owner.ToString());
			Arr.Add(MakeShared<FJsonValueObject>(Entry));
		}
		Out->SetArrayField(TEXT("types"), Arr);

		FString S;
		TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
			TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&S);
		FJsonSerializer::Serialize(Out.ToSharedRef(), Writer);
		return S;
	}
}

void HandleListCreateAssetTypesFromArgs(
	const TSharedPtr<FJsonObject>& , FString& OutJsonString, FString& )
{
	OutJsonString = BuildTypeListing();
}

namespace
{
	bool ResolveAssetType(const TSharedPtr<FJsonObject>& Obj, FString& Out)
	{
		if (Obj->TryGetStringField(TEXT("asset_type"), Out) && !Out.IsEmpty()) return true;
		if (Obj->TryGetStringField(TEXT("class"), Out)      && !Out.IsEmpty()) return true;
		FString ParentClass;
		if (Obj->TryGetStringField(TEXT("parent_class"), ParentClass) && !ParentClass.IsEmpty())
		{
			Out = TEXT("Blueprint");
			return true;
		}
		const TSharedPtr<FJsonObject>* Opt = nullptr;
		if (Obj->TryGetObjectField(TEXT("options"), Opt) && Opt && (*Opt).IsValid()
			&& (*Opt)->TryGetStringField(TEXT("parent_class"), ParentClass) && !ParentClass.IsEmpty())
		{
			Out = TEXT("Blueprint");
			return true;
		}
		return false;
	}

	TSharedPtr<FJsonObject> BuildOptions(const TSharedPtr<FJsonObject>& Obj, const TSharedPtr<FJsonObject>& OuterOptions)
	{
		TSharedPtr<FJsonObject> Out = MakeShared<FJsonObject>();
		if (OuterOptions.IsValid())
		{
			for (const auto& KV : OuterOptions->Values) Out->SetField(FString(*KV.Key), KV.Value);
		}
		const TSharedPtr<FJsonObject>* Nested = nullptr;
		if (Obj->TryGetObjectField(TEXT("options"), Nested) && Nested && Nested->IsValid())
		{
			for (const auto& KV : (*Nested)->Values) Out->SetField(FString(*KV.Key), KV.Value);
			return Out;
		}
		for (const auto& KV : Obj->Values)
		{
			const FString K(*KV.Key);
			if (K == TEXT("asset_type") || K == TEXT("type") || K == TEXT("class") ||
			    K == TEXT("name") ||
			    K == TEXT("save_path") || K == TEXT("path") ||
			    K == TEXT("action") ||
			    K == TEXT("items") || K == TEXT("assets"))
			{
				continue;
			}
			Out->SetField(K, KV.Value);
		}
		return Out;
	}

	bool CreateOneAsset(const FString& AssetType, const FString& Name, const FString& SavePath,
		const TSharedPtr<FJsonObject>& Options, FString& OutJsonString, FString& OutError)
	{
		if (!IUECPCoreModule::IsAvailable())
		{
			OutError = TEXT("create_asset: UECPCore is not available.");
			return false;
		}

		{
			const FString AssetTypeLower = AssetType.ToLower();
			if (AssetTypeLower == TEXT("folder") || AssetTypeLower == TEXT("directory"))
			{
				OutError = TEXT("'Folder' is not a content asset — UE creates folders automatically when you save an asset into the path. Just set save_path (e.g. save_path='/Game/Testing') on your create_asset calls, or use create_folder(folder_path='/Game/Testing') to make an empty one.");
				return false;
			}
		}

		{
			FText Reason;
			if (!FName::IsValidXName(Name, INVALID_OBJECTNAME_CHARACTERS, &Reason))
			{
				OutError = FString::Printf(
					TEXT("create_asset: name '%s' contains characters UE's asset tools refuse (offending set: %s — note the '.'). Pick a name using only letters, digits, and underscores."),
					*Name, INVALID_OBJECTNAME_CHARACTERS);
				return false;
			}
		}

		FString EffectivePath = SavePath;
		if (!EffectivePath.IsEmpty() && !UECPMountResolver::IsValidMountedPath(EffectivePath))
		{
			FString Tail = EffectivePath;
			while (Tail.StartsWith(TEXT("/"))) Tail = Tail.RightChop(1);
			EffectivePath = TEXT("/Game/") + Tail;
		}

		IUECPCreateAssetRegistry& Reg = IUECPCoreModule::Get().GetCreateAssetRegistry();
		const FUECPCreateAssetResult Result = Reg.Create(AssetType, Name, EffectivePath, Options, OutError);
		OutJsonString = SerialiseResult(Result);

		if (!Result.bSuccess)
		{
			UE_LOG(LogMCPTool, Warning, TEXT("create_asset type=%s name=%s — %s"),
				*AssetType, *Name, *OutError);
		}
		else
		{
			UE_LOG(LogMCPTool, Log, TEXT("create_asset type=%s -> %s"),
				*Result.AssetClass, *Result.AssetPath);
		}
		return Result.bSuccess;
	}
}

void HandleCreateAssetFromArgs(
	const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid())
	{
		OutError = TEXT("create_asset: missing arguments.");
		return;
	}

	FString OuterType;
	ResolveAssetType(Args, OuterType);
	FString OuterPath;
	Args->TryGetStringField(TEXT("save_path"), OuterPath);
	if (OuterPath.IsEmpty()) Args->TryGetStringField(TEXT("path"), OuterPath);

	TSharedPtr<FJsonObject> OuterOptions;
	const TSharedPtr<FJsonObject>* OuterOptObj = nullptr;
	if (Args->TryGetObjectField(TEXT("options"), OuterOptObj) && OuterOptObj && OuterOptObj->IsValid())
	{
		OuterOptions = *OuterOptObj;
	}

	const TArray<TSharedPtr<FJsonValue>>* ItemsArray = nullptr;
	if (BatchToolHelper::TryGetBatchItems(Args, TEXT("assets"), ItemsArray))
	{
		BatchToolHelper::FBatchResultBuilder Batch;
		for (int32 i = 0; i < ItemsArray->Num(); ++i)
		{
			TSharedPtr<FJsonObject> Item = (*ItemsArray)[i]->AsObject();
			if (!Item.IsValid()) { Batch.AddFailure(i, TEXT("Invalid item (expected object)")); continue; }

			FString ItemType;
			if (!ResolveAssetType(Item, ItemType)) ItemType = OuterType;
			if (ItemType.IsEmpty()) { Batch.AddFailure(i, TEXT("Missing asset_type")); continue; }

			FString ItemName;
			if (!Item->TryGetStringField(TEXT("name"), ItemName) || ItemName.IsEmpty())
			{
				Batch.AddFailure(i, TEXT("Missing name"));
				continue;
			}

			FString ItemPath;
			if (!Item->TryGetStringField(TEXT("save_path"), ItemPath) || ItemPath.IsEmpty())
			{
				Item->TryGetStringField(TEXT("path"), ItemPath);
			}
			if (ItemPath.IsEmpty()) ItemPath = OuterPath;

			TSharedPtr<FJsonObject> ItemOptions = BuildOptions(Item, OuterOptions);

			FString ItemOut, ItemErr;
			const bool bOk = CreateOneAsset(ItemType, ItemName, ItemPath, ItemOptions, ItemOut, ItemErr);
			if (bOk)
			{
				TSharedPtr<FJsonObject> ParsedItem;
				TSharedRef<TJsonReader<>> Rdr = TJsonReaderFactory<>::Create(ItemOut);
				if (FJsonSerializer::Deserialize(Rdr, ParsedItem) && ParsedItem.IsValid())
				{
					Batch.AddSuccess(i, ParsedItem);
				}
				else
				{
					TSharedPtr<FJsonObject> Extra = MakeShared<FJsonObject>();
					Extra->SetStringField(TEXT("name"), ItemName);
					Batch.AddSuccess(i, Extra);
				}
			}
			else
			{
				Batch.AddFailure(i, ItemErr.IsEmpty() ? TEXT("Create failed") : ItemErr);
			}
		}
		Batch.Finalize(OutJsonString);
		return;
	}

	FString AssetType = OuterType;
	if (AssetType.IsEmpty())
	{
		OutError = TEXT("create_asset: 'asset_type' is required. Call list_create_asset_types to see registered types.");
		return;
	}

	FString Name;
	Args->TryGetStringField(TEXT("name"), Name);
	if (Name.IsEmpty())
	{
		OutError = TEXT("create_asset: 'name' is required.");
		return;
	}

	TSharedPtr<FJsonObject> Options = BuildOptions(Args, OuterOptions);
	CreateOneAsset(AssetType, Name, OuterPath, Options, OutJsonString, OutError);
}

}
