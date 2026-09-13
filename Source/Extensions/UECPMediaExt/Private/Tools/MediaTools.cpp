// Copyright 2026, BlueprintsLab, All rights reserved

#include "Tools/MediaTools.h"
#include "Tools/BatchToolHelper.h"
#include "Editor.h"
#include "MediaPlayer.h"
#include "MediaTexture.h"
#include "FileMediaSource.h"
#include "StreamMediaSource.h"
#include "MediaSource.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Misc/PackageName.h"
#include "UObject/SavePackage.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"

DEFINE_LOG_CATEGORY_STATIC(LogMediaTools, Log, All);

namespace MediaTools
{

static UObject* CreateMediaAsset(UClass* AssetClass, const FString& Name,
	const FString& SavePath, FString& OutError)
{
	FString PathStr = SavePath;
	while (PathStr.EndsWith(TEXT("/"))) PathStr = PathStr.LeftChop(1);
	FString PackagePath = FString::Printf(TEXT("%s/%s"), *PathStr, *Name);

	UPackage* Package = CreatePackage(*PackagePath);
	if (!Package) { OutError = FString::Printf(TEXT("Failed to create package: %s"), *PackagePath); return nullptr; }

	UObject* Asset = NewObject<UObject>(Package, AssetClass, *Name, RF_Public | RF_Standalone | RF_Transactional);
	if (!Asset) { OutError = FString::Printf(TEXT("Failed to create %s"), *AssetClass->GetName()); return nullptr; }

	FAssetRegistryModule::AssetCreated(Asset);
	Package->MarkPackageDirty();

	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
	FString Filename = FPackageName::LongPackageNameToFilename(PackagePath, FPackageName::GetAssetPackageExtension());
	UPackage::SavePackage(Package, Asset, *Filename, SaveArgs);
	return Asset;
}

static FString GetPackagePathFromNameAndSavePath(const FString& Name, const FString& SavePath)
{
	FString PathStr = SavePath;
	while (PathStr.EndsWith(TEXT("/"))) PathStr = PathStr.LeftChop(1);
	return FString::Printf(TEXT("%s/%s"), *PathStr, *Name);
}

static void CreateSingleMediaPlayer(const FString& Name, const FString& SavePath,
	FString& OutJsonString, FString& OutError)
{
	if (Name.IsEmpty() || SavePath.IsEmpty()) { OutError = TEXT("name and save_path are required"); return; }

	UObject* Asset = CreateMediaAsset(UMediaPlayer::StaticClass(), Name, SavePath, OutError);
	if (!Asset) return;

	FString PackagePath = GetPackagePathFromNameAndSavePath(Name, SavePath);
	OutJsonString = FString::Printf(TEXT("{\"success\":true,\"asset_path\":\"%s\"}"), *PackagePath);
	UE_LOG(LogMediaTools, Log, TEXT("create_media_player: %s"), *PackagePath);
}

void HandleCreateMediaPlayerFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	const TArray<TSharedPtr<FJsonValue>>* ItemsArray = nullptr;
	if (BatchToolHelper::TryGetBatchItems(Args, TEXT("players"), ItemsArray))
	{
		BatchToolHelper::FBatchResultBuilder Batch;
		for (int32 i = 0; i < ItemsArray->Num(); i++)
		{
			TSharedPtr<FJsonObject> Item = (*ItemsArray)[i]->AsObject();
			if (!Item.IsValid()) { Batch.AddFailure(i, TEXT("Invalid item")); continue; }
			FString Name = BatchToolHelper::GetItemString(Item, TEXT("name"));
			FString SavePath = BatchToolHelper::GetItemString(Item, TEXT("save_path"));
			FString ItemOut, ItemErr;
			CreateSingleMediaPlayer(Name, SavePath, ItemOut, ItemErr);
			if (ItemErr.IsEmpty()) { auto E = MakeShared<FJsonObject>(); E->SetStringField(TEXT("name"), Name); Batch.AddSuccess(i, E); }
			else Batch.AddFailure(i, ItemErr);
		}
		Batch.Finalize(OutJsonString);
		return;
	}
	FString Name, SavePath;
	Args->TryGetStringField(TEXT("name"), Name);
	Args->TryGetStringField(TEXT("save_path"), SavePath);
	CreateSingleMediaPlayer(Name, SavePath, OutJsonString, OutError);
}

static void CreateSingleFileMediaSource(const FString& Name, const FString& SavePath,
	const FString& FilePath, FString& OutJsonString, FString& OutError)
{
	if (Name.IsEmpty() || SavePath.IsEmpty()) { OutError = TEXT("name and save_path are required"); return; }

	UObject* Asset = CreateMediaAsset(UFileMediaSource::StaticClass(), Name, SavePath, OutError);
	if (!Asset) return;

	UFileMediaSource* Source = Cast<UFileMediaSource>(Asset);
	if (Source && !FilePath.IsEmpty())
	{
		Source->SetFilePath(FilePath);
		Source->MarkPackageDirty();
	}

	FString PackagePath = GetPackagePathFromNameAndSavePath(Name, SavePath);
	OutJsonString = FString::Printf(TEXT("{\"success\":true,\"asset_path\":\"%s\",\"file_path\":\"%s\"}"),
		*PackagePath, *FilePath);
	UE_LOG(LogMediaTools, Log, TEXT("create_file_media_source: %s"), *PackagePath);
}

void HandleCreateFileMediaSourceFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	const TArray<TSharedPtr<FJsonValue>>* ItemsArray = nullptr;
	if (BatchToolHelper::TryGetBatchItems(Args, TEXT("sources"), ItemsArray))
	{
		BatchToolHelper::FBatchResultBuilder Batch;
		for (int32 i = 0; i < ItemsArray->Num(); i++)
		{
			TSharedPtr<FJsonObject> Item = (*ItemsArray)[i]->AsObject();
			if (!Item.IsValid()) { Batch.AddFailure(i, TEXT("Invalid item")); continue; }
			FString Name = BatchToolHelper::GetItemString(Item, TEXT("name"));
			FString SavePath = BatchToolHelper::GetItemString(Item, TEXT("save_path"));
			FString FilePath = BatchToolHelper::GetItemString(Item, TEXT("file_path"));
			FString ItemOut, ItemErr;
			CreateSingleFileMediaSource(Name, SavePath, FilePath, ItemOut, ItemErr);
			if (ItemErr.IsEmpty()) { auto E = MakeShared<FJsonObject>(); E->SetStringField(TEXT("name"), Name); Batch.AddSuccess(i, E); }
			else Batch.AddFailure(i, ItemErr);
		}
		Batch.Finalize(OutJsonString);
		return;
	}
	FString Name, SavePath, FilePath;
	Args->TryGetStringField(TEXT("name"), Name);
	Args->TryGetStringField(TEXT("save_path"), SavePath);
	Args->TryGetStringField(TEXT("file_path"), FilePath);
	CreateSingleFileMediaSource(Name, SavePath, FilePath, OutJsonString, OutError);
}

static void CreateSingleStreamMediaSource(const FString& Name, const FString& SavePath,
	const FString& StreamUrl, FString& OutJsonString, FString& OutError)
{
	if (Name.IsEmpty() || SavePath.IsEmpty()) { OutError = TEXT("name and save_path are required"); return; }

	UObject* Asset = CreateMediaAsset(UStreamMediaSource::StaticClass(), Name, SavePath, OutError);
	if (!Asset) return;

	UStreamMediaSource* Source = Cast<UStreamMediaSource>(Asset);
	if (Source && !StreamUrl.IsEmpty())
	{
		Source->StreamUrl = StreamUrl;
		Source->MarkPackageDirty();
	}

	FString PackagePath = GetPackagePathFromNameAndSavePath(Name, SavePath);
	OutJsonString = FString::Printf(TEXT("{\"success\":true,\"asset_path\":\"%s\",\"stream_url\":\"%s\"}"),
		*PackagePath, *StreamUrl);
	UE_LOG(LogMediaTools, Log, TEXT("create_stream_media_source: %s"), *PackagePath);
}

void HandleCreateStreamMediaSourceFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	const TArray<TSharedPtr<FJsonValue>>* ItemsArray = nullptr;
	if (BatchToolHelper::TryGetBatchItems(Args, TEXT("sources"), ItemsArray))
	{
		BatchToolHelper::FBatchResultBuilder Batch;
		for (int32 i = 0; i < ItemsArray->Num(); i++)
		{
			TSharedPtr<FJsonObject> Item = (*ItemsArray)[i]->AsObject();
			if (!Item.IsValid()) { Batch.AddFailure(i, TEXT("Invalid item")); continue; }
			FString Name = BatchToolHelper::GetItemString(Item, TEXT("name"));
			FString SavePath = BatchToolHelper::GetItemString(Item, TEXT("save_path"));
			FString StreamUrl = BatchToolHelper::GetItemString(Item, TEXT("stream_url"));
			FString ItemOut, ItemErr;
			CreateSingleStreamMediaSource(Name, SavePath, StreamUrl, ItemOut, ItemErr);
			if (ItemErr.IsEmpty()) { auto E = MakeShared<FJsonObject>(); E->SetStringField(TEXT("name"), Name); Batch.AddSuccess(i, E); }
			else Batch.AddFailure(i, ItemErr);
		}
		Batch.Finalize(OutJsonString);
		return;
	}
	FString Name, SavePath, StreamUrl;
	Args->TryGetStringField(TEXT("name"), Name);
	Args->TryGetStringField(TEXT("save_path"), SavePath);
	Args->TryGetStringField(TEXT("stream_url"), StreamUrl);
	CreateSingleStreamMediaSource(Name, SavePath, StreamUrl, OutJsonString, OutError);
}

void HandleCreateMediaTextureFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	FString Name, SavePath, PlayerPath;
	Args->TryGetStringField(TEXT("name"), Name);
	Args->TryGetStringField(TEXT("save_path"), SavePath);
	Args->TryGetStringField(TEXT("player_path"), PlayerPath);

	if (Name.IsEmpty() || SavePath.IsEmpty()) { OutError = TEXT("name and save_path are required"); return; }

	UObject* Asset = CreateMediaAsset(UMediaTexture::StaticClass(), Name, SavePath, OutError);
	if (!Asset) return;

	UMediaTexture* Texture = Cast<UMediaTexture>(Asset);
	if (Texture && !PlayerPath.IsEmpty())
	{
		UMediaPlayer* Player = LoadObject<UMediaPlayer>(nullptr, *PlayerPath);
		if (Player)
		{
			Texture->SetMediaPlayer(Player);
			Texture->MarkPackageDirty();
		}
		else
		{
			UE_LOG(LogMediaTools, Warning, TEXT("create_media_texture: player not found at %s — texture created without link"), *PlayerPath);
		}
	}

	FString PackagePath = GetPackagePathFromNameAndSavePath(Name, SavePath);
	OutJsonString = FString::Printf(TEXT("{\"success\":true,\"asset_path\":\"%s\",\"linked_player\":\"%s\"}"),
		*PackagePath, *PlayerPath);
	UE_LOG(LogMediaTools, Log, TEXT("create_media_texture: %s"), *PackagePath);
}

void HandleLinkTextureToPlayer(const FString& TexturePath, const FString& PlayerPath,
	FString& OutJsonString, FString& OutError)
{
	if (TexturePath.IsEmpty() || PlayerPath.IsEmpty())
	{
		OutError = TEXT("texture_path and player_path are required");
		return;
	}

	UMediaTexture* Texture = LoadObject<UMediaTexture>(nullptr, *TexturePath);
	if (!Texture) { OutError = FString::Printf(TEXT("UMediaTexture not found: %s"), *TexturePath); return; }

	UMediaPlayer* Player = LoadObject<UMediaPlayer>(nullptr, *PlayerPath);
	if (!Player) { OutError = FString::Printf(TEXT("UMediaPlayer not found: %s"), *PlayerPath); return; }

	Texture->SetMediaPlayer(Player);
	Texture->MarkPackageDirty();

	OutJsonString = FString::Printf(
		TEXT("{\"success\":true,\"texture_path\":\"%s\",\"player_path\":\"%s\"}"),
		*TexturePath, *PlayerPath);
	UE_LOG(LogMediaTools, Log, TEXT("link_texture_to_player: %s -> %s"), *TexturePath, *PlayerPath);
}

void HandleSetMediaPlayerProperties(const FString& PlayerPath, const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (PlayerPath.IsEmpty()) { OutError = TEXT("player_path is required"); return; }

	UMediaPlayer* Player = LoadObject<UMediaPlayer>(nullptr, *PlayerPath);
	if (!Player) { OutError = FString::Printf(TEXT("UMediaPlayer not found: %s"), *PlayerPath); return; }

	Player->Modify();

	bool bLoop;
	if (Args->TryGetBoolField(TEXT("looping"), bLoop))
	{
		if (FBoolProperty* LoopProp = FindFProperty<FBoolProperty>(UMediaPlayer::StaticClass(), TEXT("Loop")))
			LoopProp->SetPropertyValue_InContainer(Player, bLoop);
	}

	bool bPlayOnOpen;
	if (Args->TryGetBoolField(TEXT("play_on_open"), bPlayOnOpen))
		Player->PlayOnOpen = bPlayOnOpen;

	bool bNativeAudioOut;
	if (Args->TryGetBoolField(TEXT("native_audio_out"), bNativeAudioOut))
		Player->NativeAudioOut = bNativeAudioOut;

	Player->MarkPackageDirty();

	OutJsonString = FString::Printf(TEXT("{\"success\":true,\"player_path\":\"%s\"}"), *PlayerPath);
	UE_LOG(LogMediaTools, Log, TEXT("set_media_player_properties: %s"), *PlayerPath);
}

void HandleGetMediaPlayerInfo(const FString& PlayerPath, FString& OutJsonString, FString& OutError)
{
	if (PlayerPath.IsEmpty()) { OutError = TEXT("player_path is required"); return; }

	UMediaPlayer* Player = LoadObject<UMediaPlayer>(nullptr, *PlayerPath);
	if (!Player) { OutError = FString::Printf(TEXT("UMediaPlayer not found: %s"), *PlayerPath); return; }

	TSharedPtr<FJsonObject> Res = MakeShareable(new FJsonObject());
	Res->SetBoolField(TEXT("success"), true);
	Res->SetStringField(TEXT("asset_path"), PlayerPath);
	bool bLooping = false;
	if (FBoolProperty* LoopProp = FindFProperty<FBoolProperty>(UMediaPlayer::StaticClass(), TEXT("Loop")))
		bLooping = LoopProp->GetPropertyValue_InContainer(Player);
	Res->SetBoolField(TEXT("looping"), bLooping);
	Res->SetBoolField(TEXT("play_on_open"), Player->PlayOnOpen);
	Res->SetBoolField(TEXT("native_audio_out"), Player->NativeAudioOut);
	Res->SetStringField(TEXT("current_url"), Player->GetUrl());

	TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&OutJsonString);
	FJsonSerializer::Serialize(Res.ToSharedRef(), W);
}

void HandleOpenMediaSource(const FString& PlayerPath, const FString& SourcePath,
	FString& OutJsonString, FString& OutError)
{
	if (PlayerPath.IsEmpty() || SourcePath.IsEmpty())
	{
		OutError = TEXT("player_path and source_path are required");
		return;
	}

	UMediaPlayer* Player = LoadObject<UMediaPlayer>(nullptr, *PlayerPath);
	if (!Player) { OutError = FString::Printf(TEXT("UMediaPlayer not found: %s"), *PlayerPath); return; }

	UMediaSource* Source = LoadObject<UMediaSource>(nullptr, *SourcePath);
	if (!Source) { OutError = FString::Printf(TEXT("UMediaSource not found: %s"), *SourcePath); return; }

	bool bOk = Player->OpenSource(Source);

	OutJsonString = FString::Printf(
		TEXT("{\"success\":%s,\"player_path\":\"%s\",\"source_path\":\"%s\",\"note\":\"open_media_source is effective during PIE; returns %s in editor mode\"}"),
		bOk ? TEXT("true") : TEXT("false"),
		*PlayerPath, *SourcePath,
		bOk ? TEXT("true") : TEXT("false — normal for editor mode"));
	UE_LOG(LogMediaTools, Log, TEXT("open_media_source: %s on %s, result=%d"), *SourcePath, *PlayerPath, (int32)bOk);
}

void HandleLinkTextureToPlayerFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString TexturePath, PlayerPath;
	Args->TryGetStringField(TEXT("texture_path"), TexturePath);
	Args->TryGetStringField(TEXT("player_path"), PlayerPath);
	HandleLinkTextureToPlayer(TexturePath, PlayerPath, OutJsonString, OutError);
}

void HandleSetMediaPlayerPropertiesFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString PlayerPath;
	Args->TryGetStringField(TEXT("player_path"), PlayerPath);
	HandleSetMediaPlayerProperties(PlayerPath, Args, OutJsonString, OutError);
}

void HandleGetMediaPlayerInfoFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString PlayerPath;
	Args->TryGetStringField(TEXT("player_path"), PlayerPath);
	HandleGetMediaPlayerInfo(PlayerPath, OutJsonString, OutError);
}

void HandleOpenMediaSourceFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString PlayerPath, SourcePath;
	Args->TryGetStringField(TEXT("player_path"), PlayerPath);
	Args->TryGetStringField(TEXT("source_path"), SourcePath);
	HandleOpenMediaSource(PlayerPath, SourcePath, OutJsonString, OutError);
}

}
