// Copyright 2026, BlueprintsLab, All rights reserved

#include "UECPMassEntityExtModule.h"
#include "Tools/MassEntityTools.h"
#include "UECPCoreModule.h"
#include "Services/IUECPToolDispatcher.h"
#include "Services/IUECPCreateAssetRegistry.h"

DEFINE_LOG_CATEGORY(LogUECPMassEntityExt);

namespace
{
	static const TArray<FName>& OwnedToolNames()
	{
		static const TArray<FName> Names = {
			TEXT("add_mass_trait"),
			TEXT("set_mass_trait_property"),
			TEXT("get_mass_entity_config_summary"),
			TEXT("place_mass_spawner"),
			TEXT("configure_mass_spawner"),
			TEXT("add_mass_agent_component"),
			TEXT("list_mass_trait_classes"),
			TEXT("validate_mass_entity_config"),
			TEXT("remove_mass_trait"),
			TEXT("get_mass_spawner_summary"),
			TEXT("set_spawner_entity_types"),
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

void FUECPMassEntityExtModule::StartupModule()
{
	if (!IUECPCoreModule::IsAvailable()) return;
	IUECPToolDispatcher& D = IUECPCoreModule::Get().GetToolDispatcher();

	D.RegisterHandler(TEXT("add_mass_trait"),                MakeHandler(MassEntityTools::HandleAddMassTraitFromArgs));
	D.RegisterHandler(TEXT("set_mass_trait_property"),       MakeHandler(MassEntityTools::HandleSetMassTraitPropertyFromArgs));
	D.RegisterHandler(TEXT("get_mass_entity_config_summary"),MakeHandler(MassEntityTools::HandleGetMassEntityConfigSummaryFromArgs));
	D.RegisterHandler(TEXT("place_mass_spawner"),            MakeHandler(MassEntityTools::HandlePlaceMassSpawnerFromArgs));
	D.RegisterHandler(TEXT("configure_mass_spawner"),        MakeHandler(MassEntityTools::HandleConfigureMassSpawnerFromArgs));
	D.RegisterHandler(TEXT("add_mass_agent_component"),      MakeHandler(MassEntityTools::HandleAddMassAgentComponentFromArgs));
	D.RegisterHandler(TEXT("list_mass_trait_classes"),       MakeHandler(MassEntityTools::HandleListMassTraitClassesFromArgs));
	D.RegisterHandler(TEXT("validate_mass_entity_config"),   MakeHandler(MassEntityTools::HandleValidateMassEntityConfigFromArgs));
	D.RegisterHandler(TEXT("remove_mass_trait"),             MakeHandler(MassEntityTools::HandleRemoveMassTraitFromArgs));
	D.RegisterHandler(TEXT("get_mass_spawner_summary"),      MakeHandler(MassEntityTools::HandleGetMassSpawnerSummaryFromArgs));
	D.RegisterHandler(TEXT("set_spawner_entity_types"),      MakeHandler(MassEntityTools::HandleSetSpawnerEntityTypesFromArgs));

	{
		const FName U(TEXT("mass_entity"));
		const auto Meta = [&D, &U](const TCHAR* Action, const TCHAR* Summary, const TCHAR* Params)
		{ D.RegisterToolMetadata({ FName(Action), U, FString(Summary), FString(Params) }); };

		Meta(TEXT("add_mass_trait"),                TEXT("Add a trait to a MassEntityConfig (returns its editable properties)."), TEXT("config_path, trait_class"));
		Meta(TEXT("remove_mass_trait"),             TEXT("Remove a trait from a MassEntityConfig."), TEXT("config_path, trait_class"));
		Meta(TEXT("set_mass_trait_property"),       TEXT("Set a trait property (dot-notation; value as string even for numbers)."), TEXT("config_path, trait_class, property_name, value"));
		Meta(TEXT("get_mass_entity_config_summary"),TEXT("Summarise a MassEntityConfig's traits."), TEXT("config_path"));
		Meta(TEXT("validate_mass_entity_config"),   TEXT("Validate a config (run BEFORE place_mass_spawner — catches missing fragment dependencies)."), TEXT("config_path"));
		Meta(TEXT("list_mass_trait_classes"),       TEXT("List available Mass trait classes."), TEXT(""));
		Meta(TEXT("place_mass_spawner"),            TEXT("Place an AMassSpawner (single config or an entity_types mix)."), TEXT("actor_label, entity_config_path|entity_types=[{config_path,proportion}], count=100, auto_spawn?, location?, spawn_count_scale?"));
		Meta(TEXT("configure_mass_spawner"),        TEXT("Reconfigure an existing Mass spawner."), TEXT("actor_label, count, auto_spawn, spawn_count_scale"));
		Meta(TEXT("set_spawner_entity_types"),      TEXT("Set a spawner's entity-type mix (mixed crowds)."), TEXT("actor_label, entity_types"));
		Meta(TEXT("get_mass_spawner_summary"),      TEXT("Summarise a Mass spawner (omit actor_label to list all)."), TEXT("actor_label?"));
		Meta(TEXT("add_mass_agent_component"),      TEXT("Add a MassAgent component to a Blueprint."), TEXT("blueprint_path"));
	}

	IUECPCoreModule::Get().GetCreateAssetRegistry().RegisterType(TEXT("MassEntityConfig"),
		UECPCreateAsset::FactoryFromArgsFn(&MassEntityTools::HandleCreateMassEntityConfigFromArgs, TEXT("MassEntityConfig")),
		TEXT("MassEntity"));

	UE_LOG(LogUECPMassEntityExt, Log, TEXT("Registered %d Mass Entity tools"), OwnedToolNames().Num());
}

void FUECPMassEntityExtModule::ShutdownModule()
{
	if (!IUECPCoreModule::IsAvailable()) return;
	IUECPToolDispatcher& D = IUECPCoreModule::Get().GetToolDispatcher();
	for (const FName& N : OwnedToolNames()) D.UnregisterHandler(N);
	IUECPCoreModule::Get().GetCreateAssetRegistry().UnregisterType(TEXT("MassEntityConfig"));
}

IMPLEMENT_MODULE(FUECPMassEntityExtModule, UECPMassEntityExt)
