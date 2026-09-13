// Copyright 2026, BlueprintsLab, All rights reserved

#include "UECPInputExtModule.h"

#include "Tools/InputSystemTools.h"

#include "UECPCoreModule.h"
#include "Services/IUECPToolDispatcher.h"
#include "Services/IUECPCreateAssetRegistry.h"

DEFINE_LOG_CATEGORY(LogUECPInputExt);

namespace
{
	static const TArray<FName>& OwnedToolNames()
	{
		static const TArray<FName> Names = {
			TEXT("add_input_mapping"),
			TEXT("remove_input_mapping"),
			TEXT("get_input_mapping_summary"),
			TEXT("set_input_action_properties"),
			TEXT("get_input_action_summary"),
			TEXT("setup_enhanced_input"),
			TEXT("create_input_action"),
			TEXT("create_input_mapping_context"),
			TEXT("create_action"),
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

void FUECPInputExtModule::StartupModule()
{
	if (!IUECPCoreModule::IsAvailable()) return;
	IUECPToolDispatcher& D = IUECPCoreModule::Get().GetToolDispatcher();

	D.RegisterHandler(TEXT("add_input_mapping"),            MakeHandler(InputSystemTools::HandleAddInputMappingFromArgs));
	D.RegisterHandler(TEXT("remove_input_mapping"),         MakeHandler(InputSystemTools::HandleRemoveInputMappingFromArgs));
	D.RegisterHandler(TEXT("get_input_mapping_summary"),    MakeHandler(InputSystemTools::HandleGetInputMappingSummaryFromArgs));
	D.RegisterHandler(TEXT("set_input_action_properties"),  MakeHandler(InputSystemTools::HandleSetInputActionPropertiesFromArgs));
	D.RegisterHandler(TEXT("get_input_action_summary"),     MakeHandler(InputSystemTools::HandleGetInputActionSummaryFromArgs));
	D.RegisterHandler(TEXT("setup_enhanced_input"),         MakeHandler(InputSystemTools::HandleSetupEnhancedInputFromArgs));

	D.RegisterHandler(TEXT("create_input_action"),          MakeHandler(InputSystemTools::HandleCreateInputActionFromArgs));
	D.RegisterHandler(TEXT("create_input_mapping_context"), MakeHandler(InputSystemTools::HandleCreateInputMappingContextFromArgs));
	D.RegisterHandler(TEXT("create_action"),                MakeHandler(InputSystemTools::HandleCreateInputActionFromArgs));

	{
		IUECPCreateAssetRegistry& Reg = IUECPCoreModule::Get().GetCreateAssetRegistry();
		const FName ExtId(TEXT("Input"));
		Reg.RegisterType(TEXT("InputAction"),         UECPCreateAsset::FactoryFromArgsFn(&InputSystemTools::HandleCreateInputActionFromArgs,         TEXT("InputAction")),         ExtId);
		Reg.RegisterType(TEXT("InputMappingContext"), UECPCreateAsset::FactoryFromArgsFn(&InputSystemTools::HandleCreateInputMappingContextFromArgs, TEXT("InputMappingContext")), ExtId);
	}

	{
		const FName U(TEXT("input_system"));
		const auto Meta = [&D, &U](const TCHAR* Action, const TCHAR* Summary, const TCHAR* Params)
		{ D.RegisterToolMetadata({ FName(Action), U, FString(Summary), FString(Params) }); };

		Meta(TEXT("setup_enhanced_input"),          TEXT("Create all IAs + IMC + mappings in one call (preferred)."), TEXT("save_path, input_actions=[{name,value_type}], imc_name, mappings=[{action_name,key,...}]"));
		Meta(TEXT("create_input_action"),           TEXT("Create an Input Action asset."), TEXT("name, save_path, value_type?"));
		Meta(TEXT("create_action"),                 TEXT("Alias of create_input_action."), TEXT("name, save_path, value_type?"));
		Meta(TEXT("create_input_mapping_context"),  TEXT("Create an Input Mapping Context asset."), TEXT("name, save_path"));
		Meta(TEXT("add_input_mapping"),             TEXT("Map a key to an action (idempotent — replaces whole row incl. modifiers)."), TEXT("context_path, action_path, key, swizzle_axis?, scale?, negate?, trigger_type?, modifiers?, triggers?"));
		Meta(TEXT("remove_input_mapping"),          TEXT("Remove a key mapping from a context."), TEXT("context_path, action_path, key"));
		Meta(TEXT("get_input_mapping_summary"),     TEXT("Inspect an Input Mapping Context."), TEXT("context_path"));
		Meta(TEXT("set_input_action_properties"),   TEXT("Change an IA's value_type/consume/paused (no delete+recreate)."), TEXT("action_path, value_type?, consume_input?, trigger_when_paused?"));
		Meta(TEXT("get_input_action_summary"),      TEXT("Inspect an Input Action."), TEXT("action_path"));
	}

	UE_LOG(LogUECPInputExt, Log, TEXT("Registered %d Input tools (input_system umbrella)"),
		OwnedToolNames().Num());
}

void FUECPInputExtModule::ShutdownModule()
{
	if (!IUECPCoreModule::IsAvailable()) return;
	IUECPToolDispatcher& D = IUECPCoreModule::Get().GetToolDispatcher();
	for (const FName& N : OwnedToolNames()) D.UnregisterHandler(N);
	IUECPCreateAssetRegistry& Reg = IUECPCoreModule::Get().GetCreateAssetRegistry();
	for (const TCHAR* T : { TEXT("InputAction"), TEXT("InputMappingContext") })
	{
		Reg.UnregisterType(T);
	}
}

IMPLEMENT_MODULE(FUECPInputExtModule, UECPInputExt)
