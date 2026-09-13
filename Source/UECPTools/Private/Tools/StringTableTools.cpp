// Copyright 2026, BlueprintsLab, All rights reserved.

#include "Tools/StringTableTools.h"
#include "Tools/BatchToolHelper.h"
#include "Managers/SettingsManager.h"

#include "Internationalization/StringTable.h"
#include "Internationalization/StringTableCore.h"

#include "EditorAssetLibrary.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "UObject/Package.h"
#include "Misc/PackageName.h"
#include "Misc/EngineVersionComparison.h"

#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"

namespace StringTableTools
{

static TSharedPtr<FJsonObject> SuccessJson()
{
	TSharedPtr<FJsonObject> J = MakeShareable(new FJsonObject);
	J->SetBoolField(TEXT("success"), true);
	return J;
}

static FString SerializeJson(TSharedPtr<FJsonObject> J)
{
	FString Out;
	TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&Out);
	FJsonSerializer::Serialize(J.ToSharedRef(), W);
	return Out;
}

void HandleCreateStringTable(const FString& Name, const FString& SavePath,
	FString& OutJsonString, FString& OutError)
{
	if (Name.IsEmpty()) { OutError = TEXT("name is required"); return; }

	FString Folder = SavePath.IsEmpty() ? TEXT("/Game/Localization") : SavePath;
	if (Folder.EndsWith(TEXT("/"))) Folder.RemoveFromEnd(TEXT("/"));
	FString FullPath = Folder / Name;

	UPackage* Pkg = CreatePackage(*FullPath);
	Pkg->FullyLoad();

	UStringTable* Table = NewObject<UStringTable>(Pkg, *Name, RF_Public | RF_Standalone);
	Table->GetMutableStringTable()->SetNamespace(Name);
	Table->PostEditChange();
	Table->MarkPackageDirty();
	FAssetRegistryModule::AssetCreated(Table);
	UEditorAssetLibrary::SaveAsset(FullPath, false);

	auto J = SuccessJson();
	J->SetStringField(TEXT("asset_path"), FullPath);
	OutJsonString = SerializeJson(J);
}

void HandleAddStringTableEntry(const FString& AssetPath, const FString& Key, const FString& Value,
	FString& OutJsonString, FString& OutError)
{
	if (AssetPath.IsEmpty()) { OutError = TEXT("asset_path is required"); return; }
	if (Key.IsEmpty()) { OutError = TEXT("key is required"); return; }

	UStringTable* Table = Cast<UStringTable>(UEditorAssetLibrary::LoadAsset(AssetPath));
	if (!Table) { OutError = FString::Printf(TEXT("Could not load StringTable at: %s"), *AssetPath); return; }

#if !UE_VERSION_OLDER_THAN(5, 8, 0)
	Table->GetMutableStringTable()->SetSourceString(Key, Value, FString());
#else
	Table->GetMutableStringTable()->SetSourceString(Key, Value);
#endif
	Table->PostEditChange();
	Table->MarkPackageDirty();

	auto J = SuccessJson();
	J->SetStringField(TEXT("asset_path"), AssetPath);
	J->SetStringField(TEXT("key"), Key);
	J->SetStringField(TEXT("value"), Value);
	OutJsonString = SerializeJson(J);
}

void HandleGetStringTableEntries(const FString& AssetPath, FString& OutJsonString, FString& OutError)
{
	if (AssetPath.IsEmpty()) { OutError = TEXT("asset_path is required"); return; }

	UStringTable* Table = Cast<UStringTable>(UEditorAssetLibrary::LoadAsset(AssetPath));
	if (!Table) { OutError = FString::Printf(TEXT("Could not load StringTable at: %s"), *AssetPath); return; }

	TArray<TSharedPtr<FJsonValue>> Entries;
	Table->GetMutableStringTable()->EnumerateSourceStrings([&](const FString& K, const FString& V) -> bool
	{
		TSharedPtr<FJsonObject> E = MakeShareable(new FJsonObject);
		E->SetStringField(TEXT("key"), K);
		E->SetStringField(TEXT("value"), V);
		Entries.Add(MakeShareable(new FJsonValueObject(E)));
		return true;
	});

	auto J = SuccessJson();
	J->SetStringField(TEXT("asset_path"), AssetPath);
	J->SetNumberField(TEXT("count"), Entries.Num());
	J->SetArrayField(TEXT("entries"), Entries);
	OutJsonString = SerializeJson(J);
}

void HandleRemoveStringTableEntry(const FString& AssetPath, const FString& Key,
	FString& OutJsonString, FString& OutError)
{
	if (AssetPath.IsEmpty()) { OutError = TEXT("asset_path is required"); return; }
	if (Key.IsEmpty()) { OutError = TEXT("key is required"); return; }

	UStringTable* Table = Cast<UStringTable>(UEditorAssetLibrary::LoadAsset(AssetPath));
	if (!Table) { OutError = FString::Printf(TEXT("Could not load StringTable at: %s"), *AssetPath); return; }

	Table->GetMutableStringTable()->RemoveSourceString(Key);
	Table->PostEditChange();
	Table->MarkPackageDirty();

	auto J = SuccessJson();
	J->SetStringField(TEXT("asset_path"), AssetPath);
	J->SetStringField(TEXT("key"), Key);
	OutJsonString = SerializeJson(J);
}

void HandleEditStringTableEntry(const FString& AssetPath, const FString& Key, const FString& NewValue,
	FString& OutJsonString, FString& OutError)
{
	if (AssetPath.IsEmpty()) { OutError = TEXT("asset_path is required"); return; }
	if (Key.IsEmpty()) { OutError = TEXT("key is required"); return; }

	UStringTable* Table = Cast<UStringTable>(UEditorAssetLibrary::LoadAsset(AssetPath));
	if (!Table) { OutError = FString::Printf(TEXT("Could not load StringTable at: %s"), *AssetPath); return; }

#if !UE_VERSION_OLDER_THAN(5, 8, 0)
	Table->GetMutableStringTable()->SetSourceString(Key, NewValue, FString());
#else
	Table->GetMutableStringTable()->SetSourceString(Key, NewValue);
#endif
	Table->PostEditChange();
	Table->MarkPackageDirty();

	auto J = SuccessJson();
	J->SetStringField(TEXT("asset_path"), AssetPath);
	J->SetStringField(TEXT("key"), Key);
	J->SetStringField(TEXT("new_value"), NewValue);
	OutJsonString = SerializeJson(J);
}

void HandleCreateStringTableFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	FString Name, SavePath;
	Args->TryGetStringField(TEXT("name"), Name);
	Args->TryGetStringField(TEXT("save_path"), SavePath);
	if (SavePath.IsEmpty()) SavePath = FSettingsManager::GetDefaultSavePath();
	HandleCreateStringTable(Name, SavePath, OutJsonString, OutError);
}

void HandleGetStringTableEntriesFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	FString AssetPath;
	Args->TryGetStringField(TEXT("asset_path"), AssetPath);
	HandleGetStringTableEntries(AssetPath, OutJsonString, OutError);
}

void HandleRemoveStringTableEntryFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	FString AssetPath, Key;
	Args->TryGetStringField(TEXT("asset_path"), AssetPath);
	Args->TryGetStringField(TEXT("key"), Key);
	HandleRemoveStringTableEntry(AssetPath, Key, OutJsonString, OutError);
}

void HandleAddStringTableEntryFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	FString AssetPath;
	if (!Args->TryGetStringField(TEXT("table_path"), AssetPath) || AssetPath.IsEmpty())
		if (!Args->TryGetStringField(TEXT("asset_path"), AssetPath) || AssetPath.IsEmpty())
		{ OutError = TEXT("Missing required parameter: table_path"); return; }

	const TArray<TSharedPtr<FJsonValue>>* ItemsArray = nullptr;
	if (BatchToolHelper::TryGetBatchItems(Args, TEXT("entries"), ItemsArray))
	{
		BatchToolHelper::FBatchResultBuilder Batch;
		for (int32 i = 0; i < ItemsArray->Num(); i++)
		{
			TSharedPtr<FJsonObject> Item = (*ItemsArray)[i]->AsObject();
			if (!Item.IsValid()) { Batch.AddFailure(i, TEXT("Invalid item")); continue; }
			FString Key = BatchToolHelper::GetItemString(Item, TEXT("key"));
			FString Value = BatchToolHelper::GetItemString(Item, TEXT("value"));
			if (Key.IsEmpty()) { Batch.AddFailure(i, TEXT("Missing key")); continue; }
			FString Json, Err;
			HandleAddStringTableEntry(AssetPath, Key, Value, Json, Err);
			if (Err.IsEmpty())
			{
				auto Extra = MakeShared<FJsonObject>();
				Extra->SetStringField(TEXT("key"), Key);
				Batch.AddSuccess(i, Extra);
			}
			else Batch.AddFailure(i, Err);
		}
		Batch.Finalize(OutJsonString);
		return;
	}

	FString Key, Value;
	Args->TryGetStringField(TEXT("key"), Key);
	Args->TryGetStringField(TEXT("value"), Value);
	HandleAddStringTableEntry(AssetPath, Key, Value, OutJsonString, OutError);
}

void HandleEditStringTableEntryFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	FString AssetPath;
	if (!Args->TryGetStringField(TEXT("table_path"), AssetPath) || AssetPath.IsEmpty())
		if (!Args->TryGetStringField(TEXT("asset_path"), AssetPath) || AssetPath.IsEmpty())
		{ OutError = TEXT("Missing required parameter: table_path"); return; }

	const TArray<TSharedPtr<FJsonValue>>* ItemsArray = nullptr;
	if (BatchToolHelper::TryGetBatchItems(Args, TEXT("entries"), ItemsArray))
	{
		BatchToolHelper::FBatchResultBuilder Batch;
		for (int32 i = 0; i < ItemsArray->Num(); i++)
		{
			TSharedPtr<FJsonObject> Item = (*ItemsArray)[i]->AsObject();
			if (!Item.IsValid()) { Batch.AddFailure(i, TEXT("Invalid item")); continue; }
			FString Key = BatchToolHelper::GetItemString(Item, TEXT("key"));
			FString Value = BatchToolHelper::GetItemString(Item, TEXT("value"), TEXT("new_value"));
			if (Key.IsEmpty()) { Batch.AddFailure(i, TEXT("Missing key")); continue; }
			FString Json, Err;
			HandleEditStringTableEntry(AssetPath, Key, Value, Json, Err);
			if (Err.IsEmpty())
			{
				auto Extra = MakeShared<FJsonObject>();
				Extra->SetStringField(TEXT("key"), Key);
				Batch.AddSuccess(i, Extra);
			}
			else Batch.AddFailure(i, Err);
		}
		Batch.Finalize(OutJsonString);
		return;
	}

	FString Key, NewValue;
	Args->TryGetStringField(TEXT("key"), Key);
	if (!Args->TryGetStringField(TEXT("new_value"), NewValue))
		Args->TryGetStringField(TEXT("value"), NewValue);
	HandleEditStringTableEntry(AssetPath, Key, NewValue, OutJsonString, OutError);
}

}
