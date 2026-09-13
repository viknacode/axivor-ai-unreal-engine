// Copyright 2026, BlueprintsLab, All rights reserved

#include "UECPLiveLinkExtModule.h"
#include "Tools/LiveLinkTools.h"
#include "UECPCoreModule.h"
#include "Services/IUECPToolDispatcher.h"
#include "Services/IUECPCreateAssetRegistry.h"

DEFINE_LOG_CATEGORY(LogUECPLiveLinkExt);

namespace
{
	static const TArray<FName>& OwnedToolNames()
	{
		static const TArray<FName> Names = {
			TEXT("add_message_bus_source"),
			TEXT("get_subject_frame_data"),
			TEXT("remove_live_link_source"),
			TEXT("list_live_link_sources"),
			TEXT("list_live_link_subjects"),
			TEXT("apply_live_link_preset"),
			TEXT("get_live_link_summary"),
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

void FUECPLiveLinkExtModule::StartupModule()
{
	if (!IUECPCoreModule::IsAvailable()) return;
	IUECPToolDispatcher& D = IUECPCoreModule::Get().GetToolDispatcher();

	D.RegisterHandler(TEXT("add_message_bus_source"),  MakeHandler(LiveLinkTools::HandleAddMessageBusSourceFromArgs));
	D.RegisterHandler(TEXT("get_subject_frame_data"),  MakeHandler(LiveLinkTools::HandleGetSubjectFrameDataFromArgs));
	D.RegisterHandler(TEXT("remove_live_link_source"), MakeHandler(LiveLinkTools::HandleRemoveLiveLinkSourceFromArgs));
	D.RegisterHandler(TEXT("list_live_link_sources"),  MakeHandler(LiveLinkTools::HandleListLiveLinkSourcesFromArgs));
	D.RegisterHandler(TEXT("list_live_link_subjects"), MakeHandler(LiveLinkTools::HandleListLiveLinkSubjectsFromArgs));
	D.RegisterHandler(TEXT("apply_live_link_preset"),  MakeHandler(LiveLinkTools::HandleApplyLiveLinkPresetFromArgs));
	D.RegisterHandler(TEXT("get_live_link_summary"),   MakeHandler(LiveLinkTools::HandleGetLiveLinkSummaryFromArgs));

	IUECPCoreModule::Get().GetCreateAssetRegistry().RegisterType(TEXT("LiveLinkPreset"),
		UECPCreateAsset::FactoryFromArgsFn(&LiveLinkTools::HandleCreateLiveLinkPresetFromArgs, TEXT("LiveLinkPreset")),
		TEXT("LiveLink"));

	{
		const FName U(TEXT("live_link"));
		const auto Meta = [&D, &U](const TCHAR* Action, const TCHAR* Summary, const TCHAR* Params)
		{ D.RegisterToolMetadata({ FName(Action), U, FString(Summary), FString(Params) }); };

		Meta(TEXT("add_message_bus_source"),  TEXT("Add a Message Bus Live Link source (batch)."), TEXT("provider_address"));
		Meta(TEXT("get_subject_frame_data"),  TEXT("Get a subject's current frame data (batch)."), TEXT("subject_name"));
		Meta(TEXT("remove_live_link_source"), TEXT("Remove a Live Link source by GUID (batch)."), TEXT("source_guid"));
		Meta(TEXT("list_live_link_sources"),  TEXT("List Live Link sources."), TEXT(""));
		Meta(TEXT("list_live_link_subjects"), TEXT("List Live Link subjects."), TEXT("include_disabled?"));
		Meta(TEXT("apply_live_link_preset"),  TEXT("Apply a Live Link preset (synchronous)."), TEXT("preset_path"));
		Meta(TEXT("get_live_link_summary"),   TEXT("Summarise Live Link sources + subjects."), TEXT(""));
	}

	UE_LOG(LogUECPLiveLinkExt, Log, TEXT("Registered %d Live Link tools"), OwnedToolNames().Num());
}

void FUECPLiveLinkExtModule::ShutdownModule()
{
	if (!IUECPCoreModule::IsAvailable()) return;
	IUECPToolDispatcher& D = IUECPCoreModule::Get().GetToolDispatcher();
	for (const FName& N : OwnedToolNames()) D.UnregisterHandler(N);
	IUECPCoreModule::Get().GetCreateAssetRegistry().UnregisterType(TEXT("LiveLinkPreset"));
}

IMPLEMENT_MODULE(FUECPLiveLinkExtModule, UECPLiveLinkExt)
