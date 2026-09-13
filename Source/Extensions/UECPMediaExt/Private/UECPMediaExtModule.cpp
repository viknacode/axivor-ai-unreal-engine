// Copyright 2026, BlueprintsLab, All rights reserved

#include "UECPMediaExtModule.h"
#include "Tools/MediaTools.h"
#include "UECPCoreModule.h"
#include "Services/IUECPToolDispatcher.h"
#include "Services/IUECPCreateAssetRegistry.h"

DEFINE_LOG_CATEGORY(LogUECPMediaExt);

namespace
{
	static const TArray<FName>& OwnedToolNames()
	{
		static const TArray<FName> Names = {
			TEXT("link_texture_to_player"),
			TEXT("set_media_player_properties"),
			TEXT("get_media_player_info"),
			TEXT("open_media_source"),
		};
		return Names;
	}

	static auto MakeHandler(TFunction<void(const TSharedPtr<FJsonObject>&, FString&, FString&)> Fn)
	{
		return [Fn = MoveTemp(Fn)](const TSharedPtr<FJsonObject>& Args) -> FUECPToolResult
		{
			FUECPToolResult R;
			Fn(Args, R.ResultJson, R.ErrorMessage);
			R.bSuccess = R.ErrorMessage.IsEmpty();
			return R;
		};
	}
}

void FUECPMediaExtModule::StartupModule()
{
	if (!IUECPCoreModule::IsAvailable()) return;
	IUECPToolDispatcher& D = IUECPCoreModule::Get().GetToolDispatcher();

	D.RegisterHandler(TEXT("link_texture_to_player"),      MakeHandler(MediaTools::HandleLinkTextureToPlayerFromArgs));
	D.RegisterHandler(TEXT("set_media_player_properties"), MakeHandler(MediaTools::HandleSetMediaPlayerPropertiesFromArgs));
	D.RegisterHandler(TEXT("get_media_player_info"),       MakeHandler(MediaTools::HandleGetMediaPlayerInfoFromArgs));
	D.RegisterHandler(TEXT("open_media_source"),           MakeHandler(MediaTools::HandleOpenMediaSourceFromArgs));

	{
		IUECPCreateAssetRegistry& Reg = IUECPCoreModule::Get().GetCreateAssetRegistry();
		const FName ExtId(TEXT("Media"));
		Reg.RegisterType(TEXT("MediaPlayer"),       UECPCreateAsset::FactoryFromArgsFn(&MediaTools::HandleCreateMediaPlayerFromArgs,       TEXT("MediaPlayer")),       ExtId);
		Reg.RegisterType(TEXT("MediaTexture"),      UECPCreateAsset::FactoryFromArgsFn(&MediaTools::HandleCreateMediaTextureFromArgs,      TEXT("MediaTexture")),      ExtId);
		Reg.RegisterType(TEXT("FileMediaSource"),   UECPCreateAsset::FactoryFromArgsFn(&MediaTools::HandleCreateFileMediaSourceFromArgs,   TEXT("FileMediaSource")),   ExtId);
		Reg.RegisterType(TEXT("StreamMediaSource"), UECPCreateAsset::FactoryFromArgsFn(&MediaTools::HandleCreateStreamMediaSourceFromArgs, TEXT("StreamMediaSource")), ExtId);
	}

	{
		const FName U(TEXT("media"));
		const auto Meta = [&D, &U](const TCHAR* Action, const TCHAR* Summary, const TCHAR* Params)
		{ D.RegisterToolMetadata({ FName(Action), U, FString(Summary), FString(Params) }); };

		Meta(TEXT("link_texture_to_player"),      TEXT("Link a MediaTexture to a MediaPlayer."), TEXT("texture_path, player_path"));
		Meta(TEXT("set_media_player_properties"), TEXT("Set looping/play-on-open/native-audio on a player."), TEXT("player_path, looping?, play_on_open?, native_audio_out?"));
		Meta(TEXT("get_media_player_info"),       TEXT("Inspect a MediaPlayer."), TEXT("player_path"));
		Meta(TEXT("open_media_source"),           TEXT("Open a media source on a player (PIE only)."), TEXT("player_path, source_path"));
	}

	UE_LOG(LogUECPMediaExt, Log, TEXT("Registered %d Media tools"), OwnedToolNames().Num());
}

void FUECPMediaExtModule::ShutdownModule()
{
	if (!IUECPCoreModule::IsAvailable()) return;
	IUECPToolDispatcher& D = IUECPCoreModule::Get().GetToolDispatcher();
	for (const FName& N : OwnedToolNames()) D.UnregisterHandler(N);
	IUECPCreateAssetRegistry& Reg = IUECPCoreModule::Get().GetCreateAssetRegistry();
	for (const TCHAR* T : { TEXT("MediaPlayer"), TEXT("MediaTexture"), TEXT("FileMediaSource"), TEXT("StreamMediaSource") })
	{
		Reg.UnregisterType(T);
	}
}

IMPLEMENT_MODULE(FUECPMediaExtModule, UECPMediaExt)
