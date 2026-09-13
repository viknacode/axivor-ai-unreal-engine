// Copyright 2026, BlueprintsLab, All rights reserved

#include "UECPMaterialExtModule.h"

#include "Tools/MaterialTools.h"
#include "Tools/MaterialGraphTools.h"

#include "UECPCoreModule.h"
#include "Services/IUECPToolDispatcher.h"
#include "Services/IUECPCreateAssetRegistry.h"

DEFINE_LOG_CATEGORY(LogUECPMaterialExt);

namespace
{
	static const TArray<FName>& OwnedToolNames()
	{
		static const TArray<FName> Names = {
			TEXT("set_material_instance_parameter"),
			TEXT("get_material_instance_parameters"),
			TEXT("set_material_instance_static_switch"),
			TEXT("set_material_instance_parent"),
			TEXT("reset_material_instance_parameter"),
			TEXT("add_collection_parameter"),
			TEXT("set_collection_parameter_default"),
			TEXT("get_material_summary"),
			TEXT("list_material_node_types"),
			TEXT("create_material_from_textures"),
			TEXT("generate_pbr_material"),

			TEXT("add_material_node"),
			TEXT("connect_material_nodes"),
			TEXT("connect_material_nodes_bulk"),
			TEXT("delete_material_node"),
			TEXT("set_material_node_value"),
			TEXT("set_material_property"),
			TEXT("get_material_nodes"),
			TEXT("disconnect_material_pin"),
			TEXT("move_material_node"),
			TEXT("duplicate_material_node"),
			TEXT("auto_layout_material"),
			TEXT("find_material_node"),
			TEXT("get_material_graph_summary"),

			TEXT("set_material_parameters"),
			TEXT("add_material_parameter"),
			TEXT("set_material_parameter"),

			TEXT("validate_material"),
			TEXT("compile_material"),
			TEXT("get_material_compilation_stats"),
			TEXT("export_material_graph"),
			TEXT("import_material_graph"),
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

void FUECPMaterialExtModule::StartupModule()
{
	if (!IUECPCoreModule::IsAvailable()) return;
	IUECPToolDispatcher& D = IUECPCoreModule::Get().GetToolDispatcher();

	D.RegisterHandler(TEXT("set_material_instance_parameter"),     MakeHandler(MaterialTools::HandleSetMaterialInstanceParameterFromArgs));
	D.RegisterHandler(TEXT("get_material_instance_parameters"),    MakeHandler(MaterialTools::HandleGetMaterialInstanceParametersFromArgs));
	D.RegisterHandler(TEXT("set_material_instance_static_switch"), MakeHandler(MaterialTools::HandleSetMaterialInstanceStaticSwitchFromArgs));
	D.RegisterHandler(TEXT("set_material_instance_parent"),        MakeHandler(MaterialTools::HandleSetMaterialInstanceParentFromArgs));
	D.RegisterHandler(TEXT("reset_material_instance_parameter"),   MakeHandler(MaterialTools::HandleResetMaterialInstanceParameterFromArgs));
	D.RegisterHandler(TEXT("add_collection_parameter"),            MakeHandler(MaterialTools::HandleAddCollectionParameterFromArgs));
	D.RegisterHandler(TEXT("set_collection_parameter_default"),    MakeHandler(MaterialTools::HandleSetCollectionParameterDefaultFromArgs));
	D.RegisterHandler(TEXT("get_material_summary"),                MakeHandler(MaterialTools::HandleGetMaterialSummaryFromArgs));
	D.RegisterHandler(TEXT("list_material_node_types"),            MakeHandler(MaterialTools::HandleListMaterialNodeTypesFromArgs));
	D.RegisterHandler(TEXT("create_material_from_textures"),       MakeHandler(MaterialTools::HandleCreateMaterialFromTexturesFromArgs));
	D.RegisterHandler(TEXT("generate_pbr_material"),               MakeHandler(MaterialTools::HandleGeneratePBRMaterialFromArgs));

	D.RegisterHandler(TEXT("add_material_node"),                MakeHandler(MaterialGraphTools::HandleAddMaterialNodeFromArgs));
	D.RegisterHandler(TEXT("connect_material_nodes"),           MakeHandler(MaterialGraphTools::HandleConnectMaterialNodesFromArgs));
	D.RegisterHandler(TEXT("connect_material_nodes_bulk"),      MakeHandler(MaterialGraphTools::HandleConnectMaterialNodesBulkFromArgs));
	D.RegisterHandler(TEXT("delete_material_node"),             MakeHandler(MaterialGraphTools::HandleDeleteMaterialNodeFromArgs));
	D.RegisterHandler(TEXT("set_material_node_value"),          MakeHandler(MaterialGraphTools::HandleSetMaterialNodeValueFromArgs));
	D.RegisterHandler(TEXT("set_material_property"),            MakeHandler(MaterialGraphTools::HandleSetMaterialPropertyFromArgs));
	D.RegisterHandler(TEXT("get_material_nodes"),               MakeHandler(MaterialGraphTools::HandleGetMaterialNodesFromArgs));
	D.RegisterHandler(TEXT("disconnect_material_pin"),          MakeHandler(MaterialGraphTools::HandleDisconnectMaterialPinFromArgs));
	D.RegisterHandler(TEXT("move_material_node"),               MakeHandler(MaterialGraphTools::HandleMoveMaterialNodeFromArgs));
	D.RegisterHandler(TEXT("duplicate_material_node"),          MakeHandler(MaterialGraphTools::HandleDuplicateMaterialNodeFromArgs));
	D.RegisterHandler(TEXT("auto_layout_material"),             MakeHandler(MaterialGraphTools::HandleAutoLayoutMaterialFromArgs));
	D.RegisterHandler(TEXT("find_material_node"),               MakeHandler(MaterialGraphTools::HandleFindMaterialNodeFromArgs));
	D.RegisterHandler(TEXT("get_material_graph_summary"),       MakeHandler(MaterialGraphTools::HandleGetMaterialGraphSummaryFromArgs));

	D.RegisterHandler(TEXT("set_material_parameters"),          MakeHandler(MaterialGraphTools::HandleRejectMaterialParameterAliasFromArgs));
	D.RegisterHandler(TEXT("add_material_parameter"),           MakeHandler(MaterialGraphTools::HandleRejectMaterialParameterAliasFromArgs));
	D.RegisterHandler(TEXT("set_material_parameter"),           MakeHandler(MaterialGraphTools::HandleRejectMaterialParameterAliasFromArgs));

	D.RegisterHandler(TEXT("validate_material"),                MakeHandler(MaterialGraphTools::HandleValidateMaterialFromArgs));
	D.RegisterHandler(TEXT("compile_material"),                 MakeHandler(MaterialGraphTools::HandleValidateMaterialFromArgs));
	D.RegisterHandler(TEXT("get_material_compilation_stats"),   MakeHandler(MaterialGraphTools::HandleGetMaterialCompilationStatsFromArgs));
	D.RegisterHandler(TEXT("export_material_graph"),            MakeHandler(MaterialGraphTools::HandleExportMaterialGraphFromArgs));
	D.RegisterHandler(TEXT("import_material_graph"),            MakeHandler(MaterialGraphTools::HandleImportMaterialGraphFromArgs));

	{
		const FName U(TEXT("material"));
		const auto Meta = [&D, &U](const TCHAR* Action, const TCHAR* Summary, const TCHAR* Params)
		{ D.RegisterToolMetadata({ FName(Action), U, FString(Summary), FString(Params) }); };

		Meta(TEXT("set_material_property"),         TEXT("Set material-level properties: blend mode, shading model, material domain, two-sided, opacity mask clip value, and usage flags."), TEXT("material_path, blend_mode, shading_model, domain, two_sided, opacity_mask_clip_value, usage_flags"));
		Meta(TEXT("add_material_node"),             TEXT("Add an expression node (Multiply, TextureSample, Fresnel, parameter, etc.) to a material graph."), TEXT("material_path, node_type, position_x, position_y"));
		Meta(TEXT("connect_material_nodes"),        TEXT("Wire one material node's output pin into another node's input pin (or a material output like BaseColor)."), TEXT("material_path, from_node, from_output, to_node, to_input"));
		Meta(TEXT("set_material_node_value"),       TEXT("Set a constant/parameter value (or the Desc label) on a material expression node."), TEXT("material_path, node_index, property_name, float_value|vector_value|string_value|texture_path|bool_value"));
		Meta(TEXT("delete_material_node"),          TEXT("Delete an expression node from a material graph."), TEXT("material_path, node_index"));
		Meta(TEXT("get_material_nodes"),            TEXT("List every expression node in a material with its pins and connections."), TEXT("material_path"));
		Meta(TEXT("get_material_summary"),          TEXT("Summarise a material: usage flags, domain, blend/shading mode, opacity mask clip, parameter list."), TEXT("material_path"));
		Meta(TEXT("get_material_graph_summary"),    TEXT("Compact topology summary of a material (or material function) graph — node count, output connections, unconnected slots."), TEXT("material_path"));
		Meta(TEXT("validate_material"),             TEXT("Validate a material's graph and surface shader compile errors (not just topology)."), TEXT("material_path"));
		Meta(TEXT("compile_material"),              TEXT("Alias of validate_material (materials auto-compile; this surfaces shader errors)."), TEXT("material_path"));
		Meta(TEXT("get_material_compilation_stats"),TEXT("Material instruction/sampler/parameter counts and complexity tier without triggering a recompile."), TEXT("material_path"));
		Meta(TEXT("find_material_node"),            TEXT("Search a material graph for nodes by description, class name, or parameter name."), TEXT("material_path, desc|class_name|parameter_name"));
		Meta(TEXT("auto_layout_material"),          TEXT("Auto-arrange a material graph's nodes left-to-right by connection depth."), TEXT("material_path"));
		Meta(TEXT("set_material_instance_parameter"),TEXT("Override a scalar / vector / texture parameter on a Material Instance Constant."), TEXT("instance_path, param_name, float_value|vector_value|texture_path"));
		Meta(TEXT("set_material_instance_static_switch"), TEXT("Override a static switch parameter on a Material Instance."), TEXT("instance_path, parameter_name, value"));
		Meta(TEXT("get_material_instance_parameters"), TEXT("List a Material Instance's overridden + inherited scalar/vector/texture/switch parameters."), TEXT("instance_path"));
		Meta(TEXT("connect_material_nodes_bulk"),   TEXT("Wire many material graph edges in one call (fewer round trips than looping connect_material_nodes)."), TEXT("material_path, connections=[{from_node, from_output, to_node, to_input}]"));
		Meta(TEXT("disconnect_material_pin"),       TEXT("Disconnect a material node's input pin."), TEXT("material_path, node_index, input_pin_name"));
		Meta(TEXT("move_material_node"),            TEXT("Reposition a material expression node (expression nodes use negative pos_x; material output is at 0,0)."), TEXT("material_path, node_index, pos_x, pos_y"));
		Meta(TEXT("duplicate_material_node"),       TEXT("Duplicate a material expression node at an offset from the original."), TEXT("material_path, node_index, offset_x, offset_y"));
		Meta(TEXT("list_material_node_types"),      TEXT("List the expression node types add_material_node accepts, optionally filtered."), TEXT("filter"));
		Meta(TEXT("create_material_from_textures"), TEXT("Create a material that wires supplied textures into BaseColor/Normal/Roughness/Metallic/AO via Texture Samples (auto-detects map roles)."), TEXT("material_name, save_path, texture_paths|base_color_texture|normal_texture|roughness_texture|metallic_texture|ao_texture"));
		Meta(TEXT("generate_pbr_material"),         TEXT("AI-generate a PBR material (base color + derived Normal/Roughness/Metallic/AO + tiling params + auto Material Instance). ASYNC: assets appear ~30-90s later — do NOT query them the same turn."), TEXT("name, description, save_path, is_metallic, generate_ao"));
		Meta(TEXT("set_material_instance_parent"),  TEXT("Re-assign a Material Instance's parent; overrides whose names exist on the new parent are preserved."), TEXT("instance_path, new_parent_path"));
		Meta(TEXT("reset_material_instance_parameter"), TEXT("Clear a Material Instance override so the parameter inherits from its parent again."), TEXT("instance_path, param_name, param_type?"));
		Meta(TEXT("add_collection_parameter"),      TEXT("Add a scalar or vector parameter to a Material Parameter Collection (names must be unique across both arrays)."), TEXT("collection_path, param_name, param_type, default_value|default_color"));
		Meta(TEXT("set_collection_parameter_default"), TEXT("Set the default value of an existing Material Parameter Collection parameter."), TEXT("collection_path, param_name, param_type?, value|color"));
		Meta(TEXT("export_material_graph"),         TEXT("Serialise a material's full graph (nodes, connections, output bindings) to JSON for inspection or copying."), TEXT("material_path"));
		Meta(TEXT("import_material_graph"),         TEXT("Rebuild a material graph from an exported spec (overwrite clears first; merge offsets imported nodes)."), TEXT("material_path, nodes, connections, material_outputs, mode"));
	}

	{
		IUECPCreateAssetRegistry& Reg = IUECPCoreModule::Get().GetCreateAssetRegistry();
		const FName ExtId(TEXT("Material"));
		Reg.RegisterType(TEXT("Material"),                    UECPCreateAsset::FactoryFromArgsFn(&MaterialTools::HandleCreateMaterialFromArgs,                       TEXT("Material")),                    ExtId);
		Reg.RegisterType(TEXT("MaterialInstance"),            UECPCreateAsset::FactoryFromArgsFn(&MaterialTools::HandleCreateMaterialInstanceFromArgs,               TEXT("MaterialInstance")),            ExtId);
		Reg.RegisterType(TEXT("MaterialParameterCollection"), UECPCreateAsset::FactoryFromArgsFn(&MaterialTools::HandleCreateMaterialParameterCollectionFromArgs,    TEXT("MaterialParameterCollection")), ExtId);
		Reg.RegisterType(TEXT("ParticleMaterial"),            UECPCreateAsset::FactoryFromArgsFn(&MaterialTools::HandleCreateParticleMaterialFromArgs,               TEXT("ParticleMaterial")),            ExtId);
		Reg.RegisterType(TEXT("MaterialFunction"),            UECPCreateAsset::FactoryFromArgsFn(&MaterialGraphTools::HandleCreateMaterialFunctionFromArgs,          TEXT("MaterialFunction")),            ExtId);
		Reg.RegisterType(TEXT("MaterialLayer"),               UECPCreateAsset::FactoryFromArgsFn(&MaterialGraphTools::HandleCreateMaterialLayerFromArgs,             TEXT("MaterialLayer")),               ExtId);
		Reg.RegisterType(TEXT("MaterialLayerBlend"),          UECPCreateAsset::FactoryFromArgsFn(&MaterialGraphTools::HandleCreateMaterialLayerBlendFromArgs,        TEXT("MaterialLayerBlend")),          ExtId);
	}

	UE_LOG(LogUECPMaterialExt, Log, TEXT("Registered %d Material tools (material umbrella)"),
		OwnedToolNames().Num());
}

void FUECPMaterialExtModule::ShutdownModule()
{
	if (!IUECPCoreModule::IsAvailable()) return;
	IUECPToolDispatcher& D = IUECPCoreModule::Get().GetToolDispatcher();
	for (const FName& N : OwnedToolNames()) D.UnregisterHandler(N);
	IUECPCreateAssetRegistry& Reg = IUECPCoreModule::Get().GetCreateAssetRegistry();
	for (const TCHAR* T : { TEXT("Material"), TEXT("MaterialInstance"), TEXT("MaterialParameterCollection"),
	                        TEXT("ParticleMaterial"), TEXT("MaterialFunction"),
	                        TEXT("MaterialLayer"), TEXT("MaterialLayerBlend") })
	{
		Reg.UnregisterType(T);
	}
}

IMPLEMENT_MODULE(FUECPMaterialExtModule, UECPMaterialExt)
