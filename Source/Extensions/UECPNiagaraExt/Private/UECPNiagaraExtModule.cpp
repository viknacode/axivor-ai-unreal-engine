// Copyright 2026, BlueprintsLab, All rights reserved

#include "UECPNiagaraExtModule.h"

#include "Tools/NiagaraTools.h"
#include "Tools/NiagaraStackTools.h"
#include "Tools/NiagaraSchemaTools.h"

#include "UECPCoreModule.h"
#include "Services/IUECPToolDispatcher.h"
#include "Services/IUECPCreateAssetRegistry.h"

DEFINE_LOG_CATEGORY(LogUECPNiagaraExt);

namespace
{
	static const TArray<FName>& OwnedToolNames()
	{
		static const TArray<FName> Names = {
			TEXT("add_emitter_to_system"),
			TEXT("add_niagara_module"),
			TEXT("duplicate_emitter_in_system"),
			TEXT("remove_niagara_module"),
			TEXT("set_niagara_module_parameter"),
			TEXT("set_niagara_module_parameters_bulk"),
			TEXT("set_niagara_parameter"),
			TEXT("get_niagara_summary"),
			TEXT("list_niagara_templates"),
			TEXT("set_niagara_renderer_material"),
			TEXT("get_niagara_detailed_summary"),
			TEXT("set_niagara_renderer_property"),
			TEXT("set_mesh_renderer_mesh"),
			TEXT("validate_niagara_system"),
			TEXT("niagara_get_stack_issues"),
			TEXT("niagara_apply_stack_issue_fix"),
			TEXT("niagara_get_system_compile_state"),
			TEXT("niagara_get_system_schema"),
			TEXT("niagara_get_emitter_schema"),
			TEXT("niagara_get_renderer_schema"),
			TEXT("niagara_get_data_interface_schema"),
			TEXT("niagara_list_renderer_classes"),
			TEXT("niagara_list_data_interface_classes"),
			TEXT("niagara_add_set_parameters_module"),
			TEXT("resolve_niagara_dependencies"),
			TEXT("set_niagara_distribution_curve"),
			TEXT("set_niagara_distribution_mode"),
			TEXT("set_niagara_module_enabled"),
			TEXT("bind_niagara_module_input"),
			TEXT("unbind_niagara_module_input"),
			TEXT("set_niagara_renderer_binding"),
			TEXT("list_niagara_modules"),
			TEXT("remove_emitter_from_system"),
			TEXT("set_emitter_properties"),
			TEXT("add_renderer_to_emitter"),
			TEXT("set_niagara_system_properties"),
			TEXT("set_emitter_scalability"),
			TEXT("remove_niagara_renderer"),
			TEXT("get_emitter_modules"),
			TEXT("set_emitter_spawn_rate"),
			TEXT("set_niagara_sim_target"),
			TEXT("rename_emitter_in_system"),
			TEXT("add_niagara_event_handler"),
			TEXT("set_niagara_data_interface"),
			TEXT("add_niagara_scratch_module"),
			TEXT("get_niagara_runtime_state"),
			TEXT("cleanup_niagara_system_orphans"),
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

void FUECPNiagaraExtModule::StartupModule()
{
	if (!IUECPCoreModule::IsAvailable()) return;
	IUECPToolDispatcher& D = IUECPCoreModule::Get().GetToolDispatcher();

	D.RegisterHandler(TEXT("add_emitter_to_system"),             MakeHandler(NiagaraTools::HandleAddEmitterToSystemFromArgs));
	D.RegisterHandler(TEXT("add_niagara_module"),                MakeHandler(NiagaraTools::HandleAddNiagaraModuleFromArgs));
	D.RegisterHandler(TEXT("duplicate_emitter_in_system"),       MakeHandler(NiagaraTools::HandleDuplicateEmitterInSystemFromArgs));
	D.RegisterHandler(TEXT("remove_niagara_module"),             MakeHandler(NiagaraTools::HandleRemoveNiagaraModuleFromArgs));
	D.RegisterHandler(TEXT("set_niagara_module_parameter"),      MakeHandler(NiagaraTools::HandleSetNiagaraModuleParameterFromArgs));
	D.RegisterHandler(TEXT("set_niagara_module_parameters_bulk"),MakeHandler(NiagaraTools::HandleSetNiagaraModuleParameterFromArgs));
	D.RegisterHandler(TEXT("set_niagara_parameter"),             MakeHandler(NiagaraTools::HandleSetNiagaraParameterFromArgs));
	D.RegisterHandler(TEXT("get_niagara_summary"),               MakeHandler(NiagaraTools::HandleGetNiagaraSummaryFromArgs));
	D.RegisterHandler(TEXT("list_niagara_templates"),            MakeHandler(NiagaraTools::HandleListNiagaraTemplatesFromArgs));
	D.RegisterHandler(TEXT("set_niagara_renderer_material"),     MakeHandler(NiagaraTools::HandleSetNiagaraRendererMaterialFromArgs));
	D.RegisterHandler(TEXT("get_niagara_detailed_summary"),      MakeHandler(NiagaraTools::HandleGetNiagaraDetailedSummaryFromArgs));
	D.RegisterHandler(TEXT("set_niagara_renderer_property"),     MakeHandler(NiagaraTools::HandleSetNiagaraRendererPropertyFromArgs));
	D.RegisterHandler(TEXT("set_mesh_renderer_mesh"),            MakeHandler(NiagaraTools::HandleSetMeshRendererMeshFromArgs));
	D.RegisterHandler(TEXT("validate_niagara_system"),           MakeHandler(NiagaraTools::HandleValidateNiagaraSystemFromArgs));
	D.RegisterHandler(TEXT("niagara_get_stack_issues"),          MakeHandler(NiagaraStackTools::HandleGetStackIssuesFromArgs));
	D.RegisterHandler(TEXT("niagara_apply_stack_issue_fix"),     MakeHandler(NiagaraStackTools::HandleApplyStackIssueFixFromArgs));
	D.RegisterHandler(TEXT("niagara_get_system_compile_state"),  MakeHandler(NiagaraStackTools::HandleGetSystemCompileStateFromArgs));
	D.RegisterHandler(TEXT("niagara_get_system_schema"),         MakeHandler(NiagaraSchemaTools::HandleGetSystemSchemaFromArgs));
	D.RegisterHandler(TEXT("niagara_get_emitter_schema"),        MakeHandler(NiagaraSchemaTools::HandleGetEmitterSchemaFromArgs));
	D.RegisterHandler(TEXT("niagara_get_renderer_schema"),       MakeHandler(NiagaraSchemaTools::HandleGetRendererSchemaFromArgs));
	D.RegisterHandler(TEXT("niagara_get_data_interface_schema"), MakeHandler(NiagaraSchemaTools::HandleGetDataInterfaceSchemaFromArgs));
	D.RegisterHandler(TEXT("niagara_list_renderer_classes"),     MakeHandler(NiagaraSchemaTools::HandleListRendererClassesFromArgs));
	D.RegisterHandler(TEXT("niagara_list_data_interface_classes"), MakeHandler(NiagaraSchemaTools::HandleListDataInterfaceClassesFromArgs));
	D.RegisterHandler(TEXT("niagara_add_set_parameters_module"), MakeHandler(NiagaraSchemaTools::HandleAddSetParametersModuleFromArgs));
	D.RegisterHandler(TEXT("resolve_niagara_dependencies"),      MakeHandler(NiagaraTools::HandleResolveNiagaraDependenciesFromArgs));
	D.RegisterHandler(TEXT("set_niagara_distribution_curve"),    MakeHandler(NiagaraTools::HandleSetNiagaraDistributionCurveFromArgs));
	D.RegisterHandler(TEXT("set_niagara_distribution_mode"),     MakeHandler(NiagaraTools::HandleSetNiagaraDistributionModeFromArgs));
	D.RegisterHandler(TEXT("set_niagara_module_enabled"),        MakeHandler(NiagaraTools::HandleSetNiagaraModuleEnabledFromArgs));
	D.RegisterHandler(TEXT("bind_niagara_module_input"),         MakeHandler(NiagaraTools::HandleBindNiagaraModuleInputFromArgs));
	D.RegisterHandler(TEXT("unbind_niagara_module_input"),       MakeHandler(NiagaraTools::HandleUnbindNiagaraModuleInputFromArgs));
	D.RegisterHandler(TEXT("set_niagara_renderer_binding"),      MakeHandler(NiagaraTools::HandleSetNiagaraRendererBindingFromArgs));
	D.RegisterHandler(TEXT("list_niagara_modules"),              MakeHandler(NiagaraTools::HandleListNiagaraModulesFromArgs));
	D.RegisterHandler(TEXT("remove_emitter_from_system"),        MakeHandler(NiagaraTools::HandleRemoveEmitterFromSystemFromArgs));
	D.RegisterHandler(TEXT("set_emitter_properties"),            MakeHandler(NiagaraTools::HandleSetEmitterPropertiesFromArgs));
	D.RegisterHandler(TEXT("add_renderer_to_emitter"),           MakeHandler(NiagaraTools::HandleAddRendererToEmitterFromArgs));
	D.RegisterHandler(TEXT("set_niagara_system_properties"),     MakeHandler(NiagaraTools::HandleSetNiagaraSystemPropertiesFromArgs));
	D.RegisterHandler(TEXT("set_emitter_scalability"),           MakeHandler(NiagaraTools::HandleSetEmitterScalabilityFromArgs));
	D.RegisterHandler(TEXT("remove_niagara_renderer"),           MakeHandler(NiagaraTools::HandleRemoveNiagaraRendererFromArgs));
	D.RegisterHandler(TEXT("get_emitter_modules"),               MakeHandler(NiagaraTools::HandleGetEmitterModulesFromArgs));
	D.RegisterHandler(TEXT("set_emitter_spawn_rate"),            MakeHandler(NiagaraTools::HandleSetEmitterSpawnRateFromArgs));
	D.RegisterHandler(TEXT("set_niagara_sim_target"),            MakeHandler(NiagaraTools::HandleSetNiagaraSimTargetFromArgs));
	D.RegisterHandler(TEXT("rename_emitter_in_system"),          MakeHandler(NiagaraTools::HandleRenameEmitterInSystemFromArgs));
	D.RegisterHandler(TEXT("add_niagara_event_handler"),         MakeHandler(NiagaraTools::HandleAddNiagaraEventHandlerFromArgs));
	D.RegisterHandler(TEXT("set_niagara_data_interface"),        MakeHandler(NiagaraTools::HandleSetNiagaraDataInterfaceFromArgs));
	D.RegisterHandler(TEXT("add_niagara_scratch_module"),        MakeHandler(NiagaraTools::HandleAddNiagaraScratchModuleFromArgs));
	D.RegisterHandler(TEXT("get_niagara_runtime_state"),         MakeHandler(NiagaraTools::HandleGetNiagaraRuntimeStateFromArgs));
	D.RegisterHandler(TEXT("cleanup_niagara_system_orphans"),    MakeHandler(NiagaraTools::HandleCleanupNiagaraSystemOrphansFromArgs));

	{
		const FName U(TEXT("niagara"));
		const auto Meta = [&D, &U](const TCHAR* Action, const TCHAR* Summary, const TCHAR* Params)
		{ D.RegisterToolMetadata({ FName(Action), U, FString(Summary), FString(Params) }); };

		Meta(TEXT("add_emitter_to_system"),       TEXT("Add a classic emitter to a Niagara system from an engine template path (merge/resolve runs internally)."), TEXT("system_path, emitter_path"));
		Meta(TEXT("remove_emitter_from_system"),  TEXT("Remove an emitter from a Niagara system."), TEXT("system_path, emitter_name"));
		Meta(TEXT("duplicate_emitter_in_system"), TEXT("Duplicate an emitter within a Niagara system."), TEXT("system_path, source_emitter_name, new_emitter_name"));
		Meta(TEXT("rename_emitter_in_system"),    TEXT("Rename an emitter in a Niagara system."), TEXT("system_path, emitter_name, new_name"));
		Meta(TEXT("get_emitter_modules"),         TEXT("List an emitter's modules + emitter_type (stateless/classic); on classic, per-module inputs[] and event_handlers[]. Read before editing."), TEXT("system_path, emitter_name"));
		Meta(TEXT("set_emitter_spawn_rate"),      TEXT("Set a stateless emitter's spawn rate (and optional burst count/time)."), TEXT("system_path, emitter_name, spawn_rate, burst_count?, burst_time?"));
		Meta(TEXT("set_niagara_sim_target"),      TEXT("Set a classic emitter's sim target (CPU or GPU)."), TEXT("system_path, emitter_name, sim_target"));
		Meta(TEXT("set_emitter_properties"),      TEXT("Set emitter-level properties."), TEXT("system_path, emitter_name, properties"));
		Meta(TEXT("set_emitter_scalability"),     TEXT("Set an emitter's scalability settings."), TEXT("system_path, emitter_name, properties"));

		Meta(TEXT("add_niagara_module"),          TEXT("Add a module to an emitter script section (pre-flight rejects miswired event/particle-scope sections)."), TEXT("system_path, emitter_name, module_path, script_section?, insert_index?"));
		Meta(TEXT("remove_niagara_module"),       TEXT("Remove a module from an emitter."), TEXT("system_path, emitter_name, module_name"));
		Meta(TEXT("set_niagara_module_parameter"),TEXT("Set a module input value (stateless reflection or classic override pin). Compile feedback returned."), TEXT("system_path, emitter_name, module_name, parameter_name, *_value, script_section?"));
		Meta(TEXT("set_niagara_module_parameters_bulk"), TEXT("Set many module inputs on an emitter in one call (preferred for efficiency)."), TEXT("system_path, emitter_name, parameters=[{parameter_name, module_name, *_value}]"));
		Meta(TEXT("set_niagara_module_enabled"),  TEXT("Toggle a module's bIsEnabled without removing it (preserves its values)."), TEXT("system_path, emitter_name, module_name, enabled"));
		Meta(TEXT("set_niagara_distribution_mode"), TEXT("Switch a distribution's Mode (direct/range/nonuniform_range) and atomically write its value(s)."), TEXT("system_path, emitter_name, module_name, property_name, mode, value|min+max"));
		Meta(TEXT("set_niagara_distribution_curve"), TEXT("Set a color/scale distribution as a curve over particle life (>=2 keyframes)."), TEXT("system_path, emitter_name, module_name, property_name, curve_values"));
		Meta(TEXT("bind_niagara_module_input"),   TEXT("Bind a module input to read from another Niagara parameter (User./Particles./Emitter.) — runtime-controllable."), TEXT("system_path, emitter_name, module_name, parameter_name, source_parameter, script_section?"));
		Meta(TEXT("unbind_niagara_module_input"), TEXT("Remove a previous bind_niagara_module_input (reverts to module default; idempotent)."), TEXT("system_path, emitter_name, module_name, parameter_name, script_section?"));
		Meta(TEXT("add_niagara_scratch_module"),  TEXT("Drop a Custom HLSL scratch module into a script section (classic only). Use only when no shipped module fits."), TEXT("system_path, emitter_name, script_section, name, hlsl_code, inputs?, outputs?, event_name?"));
		Meta(TEXT("list_niagara_modules"),        TEXT("List available Niagara module assets (V1 deprecated entries filtered when a V2 exists)."), TEXT("filter?"));
		Meta(TEXT("resolve_niagara_dependencies"),TEXT("Auto-add missing dependency modules and reorder the stack to satisfy pre/post-dependency relationships."), TEXT("system_path"));

		Meta(TEXT("set_niagara_parameter"),       TEXT("Set a system-level User.* / template-exposed Niagara parameter."), TEXT("system_path, parameter_name, float_value|bool_value|int_value|vector_value|string_value"));
		Meta(TEXT("set_niagara_system_properties"), TEXT("Set system-level properties."), TEXT("system_path, properties"));
		Meta(TEXT("set_niagara_data_interface"),  TEXT("Set a data interface property on a classic emitter (empty property_name = discover)."), TEXT("system_path, emitter_name, data_interface_name, property_name, property_value"));
		Meta(TEXT("add_niagara_event_handler"),   TEXT("Add an event-handler script to a LISTENER emitter for a SOURCE emitter's event (classic only); populate the handler graph afterwards."), TEXT("system_path, emitter_name, source_emitter_name, event_name"));

		Meta(TEXT("add_renderer_to_emitter"),     TEXT("Add a renderer to an emitter (auto-applies a default material/mesh)."), TEXT("system_path, emitter_name, renderer_type=Sprite|Mesh|Ribbon|Light|Component"));
		Meta(TEXT("remove_niagara_renderer"),     TEXT("Remove a renderer from an emitter."), TEXT("system_path, emitter_name, renderer_index?"));
		Meta(TEXT("set_niagara_renderer_material"), TEXT("Set the material on a Sprite/Ribbon/Light renderer (NOT Mesh — use set_mesh_renderer_mesh)."), TEXT("system_path, emitter_name, material_path, renderer_index?"));
		Meta(TEXT("set_niagara_renderer_property"), TEXT("Set a renderer property by name (cannot set Mesh Meshes[] — use set_mesh_renderer_mesh)."), TEXT("system_path, emitter_name, property_name, *_value, renderer_index?"));
		Meta(TEXT("set_mesh_renderer_mesh"),      TEXT("Set a Mesh renderer's mesh (+ optional override material) per slot; resizes Meshes[]."), TEXT("system_path, emitter_name, mesh_path, material_path?, renderer_index?, mesh_index?"));
		Meta(TEXT("set_niagara_renderer_binding"),TEXT("Redirect which attribute a renderer reads for a visual property (e.g. ColorBinding, DynamicMaterialBinding)."), TEXT("system_path, emitter_name, binding_name, source_parameter, renderer_index?"));

		Meta(TEXT("get_niagara_summary"),         TEXT("Summarise a Niagara system (emitters, renderers, exposed params)."), TEXT("system_path"));
		Meta(TEXT("get_niagara_detailed_summary"),TEXT("Detailed Niagara system summary."), TEXT("system_path"));
		Meta(TEXT("get_niagara_runtime_state"),   TEXT("Read live runtime state from an active component (spawned counts, exec state) — distinguishes 'spawned 0' from 'spawned then died'."), TEXT("system_path"));
		Meta(TEXT("validate_niagara_system"),     TEXT("Read-only health check mirroring the editor's stack walk; returns would_open + errors/warnings. Run before declaring done."), TEXT("system_path"));
		Meta(TEXT("list_niagara_templates"),      TEXT("List engine Niagara system/emitter templates."), TEXT("filter?, systems_only?, emitters_only?"));
		Meta(TEXT("cleanup_niagara_system_orphans"), TEXT("Remove orphaned sub-objects from a Niagara system."), TEXT("system_path"));

		Meta(TEXT("niagara_get_stack_issues"),    TEXT("List the system editor's stack issues for a system."), TEXT("system_path"));
		Meta(TEXT("niagara_apply_stack_issue_fix"), TEXT("Apply a stack issue's suggested fix by id."), TEXT("system_path, issue_id, fix_id"));
		Meta(TEXT("niagara_get_system_compile_state"), TEXT("Get the system's compile state."), TEXT("system_path"));
		Meta(TEXT("niagara_get_system_schema"),   TEXT("Get the Niagara system schema (settable properties)."), TEXT(""));
		Meta(TEXT("niagara_get_emitter_schema"),  TEXT("Get the Niagara emitter schema (settable properties)."), TEXT(""));
		Meta(TEXT("niagara_get_renderer_schema"), TEXT("Get a renderer class's schema (settable properties)."), TEXT("renderer_class"));
		Meta(TEXT("niagara_get_data_interface_schema"), TEXT("Get a data interface class's schema."), TEXT("data_interface_class"));
		Meta(TEXT("niagara_list_renderer_classes"), TEXT("List available Niagara renderer classes."), TEXT(""));
		Meta(TEXT("niagara_list_data_interface_classes"), TEXT("List available Niagara data interface classes."), TEXT(""));
		Meta(TEXT("niagara_add_set_parameters_module"), TEXT("Add a 'Set Parameters' module to a script section with the given parameters."), TEXT("system_path, emitter_name, script_section, insert_index?, parameters=[{name,type,value}]"));
	}

	{
		IUECPCreateAssetRegistry& Reg = IUECPCoreModule::Get().GetCreateAssetRegistry();
		const FName ExtId(TEXT("Niagara"));
		Reg.RegisterType(TEXT("NiagaraSystem"),  UECPCreateAsset::FactoryFromArgsFn(&NiagaraTools::HandleCreateNiagaraSystemFromArgs,  TEXT("NiagaraSystem")),  ExtId);
		Reg.RegisterType(TEXT("NiagaraEmitter"), UECPCreateAsset::FactoryFromArgsFn(&NiagaraTools::HandleCreateNiagaraEmitterFromArgs, TEXT("NiagaraEmitter")), ExtId);
	}

	UE_LOG(LogUECPNiagaraExt, Log, TEXT("Registered %d Niagara tools (niagara umbrella)"),
		OwnedToolNames().Num());
}

void FUECPNiagaraExtModule::ShutdownModule()
{
	if (!IUECPCoreModule::IsAvailable()) return;
	IUECPToolDispatcher& D = IUECPCoreModule::Get().GetToolDispatcher();
	for (const FName& N : OwnedToolNames()) D.UnregisterHandler(N);
	IUECPCreateAssetRegistry& Reg = IUECPCoreModule::Get().GetCreateAssetRegistry();
	for (const TCHAR* T : { TEXT("NiagaraSystem"), TEXT("NiagaraEmitter") })
	{
		Reg.UnregisterType(T);
	}
}

IMPLEMENT_MODULE(FUECPNiagaraExtModule, UECPNiagaraExt)
