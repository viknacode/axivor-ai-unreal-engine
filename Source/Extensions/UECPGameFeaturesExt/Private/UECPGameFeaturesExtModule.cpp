// Copyright 2026, BlueprintsLab, All rights reserved

#include "UECPGameFeaturesExtModule.h"
#include "Tools/GameFeaturesTools.h"
#include "UECPCoreModule.h"
#include "Services/IUECPToolDispatcher.h"
#include "Services/IUECPCreateAssetRegistry.h"

DEFINE_LOG_CATEGORY(LogUECPGameFeaturesExt);

namespace
{
	static const TArray<FName>& OwnedToolNames()
	{
		static const TArray<FName> Names = {
			TEXT("add_game_feature_action_add_components"),
			TEXT("add_game_feature_action"),
			TEXT("list_game_feature_actions"),
			TEXT("list_game_features"),
			TEXT("set_game_feature_state"),
		};
		return Names;
	}
}

void FUECPGameFeaturesExtModule::StartupModule()
{
	UE_LOG(LogUECPGameFeaturesExt, Log, TEXT("FUECPGameFeaturesExtModule: StartupModule"));

	if (!IUECPCoreModule::IsAvailable())
	{
		UE_LOG(LogUECPGameFeaturesExt, Warning,
			TEXT("UECPCore not available — cannot register Game Features tools"));
		return;
	}

	IUECPToolDispatcher& Dispatcher = IUECPCoreModule::Get().GetToolDispatcher();

	auto MakeHandler = [](TFunction<void(const TSharedPtr<FJsonObject>&, FString&, FString&)> Fn)
	{
		return [Fn = MoveTemp(Fn)](const TSharedPtr<FJsonObject>& Args) -> FUECPToolResult
		{
			FUECPToolResult R;
			Fn(Args, R.ResultJson, R.ErrorMessage);
			R.bSuccess = R.ErrorMessage.IsEmpty();
			return R;
		};
	};

	Dispatcher.RegisterHandler(TEXT("add_game_feature_action_add_components"),
		MakeHandler(GameFeaturesTools::HandleAddGameFeatureActionAddComponentsFromArgs));
	Dispatcher.RegisterHandler(TEXT("add_game_feature_action"),
		MakeHandler(GameFeaturesTools::HandleAddGameFeatureActionFromArgs));
	Dispatcher.RegisterHandler(TEXT("list_game_feature_actions"),
		MakeHandler(GameFeaturesTools::HandleListGameFeatureActionsFromArgs));
	Dispatcher.RegisterHandler(TEXT("list_game_features"),
		MakeHandler(GameFeaturesTools::HandleListGameFeaturesFromArgs));
	Dispatcher.RegisterHandler(TEXT("set_game_feature_state"),
		MakeHandler(GameFeaturesTools::HandleSetGameFeatureStateFromArgs));

	IUECPCoreModule::Get().GetCreateAssetRegistry().RegisterType(TEXT("GameFeaturePlugin"),
		UECPCreateAsset::FactoryFromArgsFn(&GameFeaturesTools::HandleCreateGameFeaturePluginFromArgs, TEXT("GameFeaturePlugin")),
		TEXT("GameFeatures"));

	{
		const FName U(TEXT("game_features"));
		const auto Meta = [&Dispatcher, &U](const TCHAR* Action, const TCHAR* Summary, const TCHAR* Params)
		{ Dispatcher.RegisterToolMetadata({ FName(Action), U, FString(Summary), FString(Params) }); };

		Meta(TEXT("add_game_feature_action_add_components"), TEXT("Append a UGameFeatureAction_AddComponents (most common GFP action)."), TEXT("game_feature_data_path, components=[{actor_class, component_class, client?, server?}]"));
		Meta(TEXT("add_game_feature_action"),                TEXT("Append any UGameFeatureAction subclass by class path."), TEXT("game_feature_data_path, action_class"));
		Meta(TEXT("list_game_feature_actions"),              TEXT("List a GameFeatureData's actions."), TEXT("game_feature_data_path"));
		Meta(TEXT("list_game_features"),                     TEXT("List game-feature plugins + their state."), TEXT(""));
		Meta(TEXT("set_game_feature_state"),                 TEXT("Set a plugin state (active|registered|unloaded; async)."), TEXT("plugin_name, state"));
	}

	UE_LOG(LogUECPGameFeaturesExt, Log,
		TEXT("Registered %d Game Features tools with the dispatcher"), OwnedToolNames().Num());
}

void FUECPGameFeaturesExtModule::ShutdownModule()
{
	UE_LOG(LogUECPGameFeaturesExt, Log, TEXT("FUECPGameFeaturesExtModule: ShutdownModule"));

	if (!IUECPCoreModule::IsAvailable()) return;

	IUECPToolDispatcher& Dispatcher = IUECPCoreModule::Get().GetToolDispatcher();
	for (const FName& Name : OwnedToolNames())
	{
		Dispatcher.UnregisterHandler(Name);
	}
	IUECPCoreModule::Get().GetCreateAssetRegistry().UnregisterType(TEXT("GameFeaturePlugin"));
}

IMPLEMENT_MODULE(FUECPGameFeaturesExtModule, UECPGameFeaturesExt)
