// Copyright 2026, BlueprintsLab, All rights reserved

#include "UECPAudioExtModule.h"

#include "Tools/AudioTools.h"
#include "Tools/MetaSoundTools.h"

#include "UECPCoreModule.h"
#include "Services/IUECPToolDispatcher.h"
#include "Services/IUECPCreateAssetRegistry.h"

DEFINE_LOG_CATEGORY(LogUECPAudioExt);

namespace
{
	static const TArray<FName>& OwnedToolNames()
	{
		static const TArray<FName> Names = {
			TEXT("get_control_bus_summary"),
			TEXT("set_control_bus_mix_stage"),
			TEXT("add_sound_node"),
			TEXT("connect_sound_nodes"),
			TEXT("set_sound_cue_output"),
			TEXT("set_sound_cue_attenuation"),
			TEXT("set_sound_cue_sound_class"),
			TEXT("assign_sound_cue_sound_class"),
			TEXT("set_sound_class_properties"),
			TEXT("set_sound_mix_properties"),
			TEXT("set_audio_submix_properties"),
			TEXT("set_sound_concurrency_properties"),
			TEXT("assign_sound_concurrency"),
			TEXT("get_sound_cue_summary"),
			TEXT("set_attenuation_shape"),
			TEXT("set_attenuation_spatialization"),
			TEXT("add_submix_effect"),
			TEXT("remove_sound_node"),
			TEXT("set_sound_wave_properties"),
			TEXT("get_sound_wave_info"),
			TEXT("build_sound_cue_from_spec"),
			TEXT("set_sound_node_property"),
			TEXT("create_quartz_clock"),
			TEXT("set_quartz_clock_settings"),
			TEXT("get_quartz_clock_info"),
			TEXT("generate_sound"),

			TEXT("get_metasound_summary"),
			TEXT("add_metasound_input"),
			TEXT("add_metasound_output"),
			TEXT("set_metasound_default_parameter"),
			TEXT("duplicate_metasound"),
			TEXT("add_metasound_node"),
			TEXT("connect_metasound_nodes"),
			TEXT("remove_metasound_node"),
			TEXT("disconnect_metasound_nodes"),
			TEXT("list_metasound_node_types"),
			TEXT("set_metasound_node_input_default"),
			TEXT("remove_metasound_input"),
			TEXT("remove_metasound_output"),
			TEXT("list_metasound_nodes"),
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

void FUECPAudioExtModule::StartupModule()
{
	if (!IUECPCoreModule::IsAvailable()) return;
	IUECPToolDispatcher& D = IUECPCoreModule::Get().GetToolDispatcher();

	D.RegisterHandler(TEXT("get_control_bus_summary"),           MakeHandler(AudioTools::HandleGetControlBusSummaryFromArgs));
	D.RegisterHandler(TEXT("set_control_bus_mix_stage"),         MakeHandler(AudioTools::HandleSetControlBusMixStageFromArgs));
	D.RegisterHandler(TEXT("add_sound_node"),                    MakeHandler(AudioTools::HandleAddSoundNodeFromArgs));
	D.RegisterHandler(TEXT("connect_sound_nodes"),               MakeHandler(AudioTools::HandleConnectSoundNodesFromArgs));
	D.RegisterHandler(TEXT("set_sound_cue_output"),              MakeHandler(AudioTools::HandleSetSoundCueOutputFromArgs));
	D.RegisterHandler(TEXT("set_sound_cue_attenuation"),         MakeHandler(AudioTools::HandleSetSoundCueAttenuationFromArgs));
	D.RegisterHandler(TEXT("set_sound_cue_sound_class"),         MakeHandler(AudioTools::HandleSetSoundCueSoundClassFromArgs));
	D.RegisterHandler(TEXT("assign_sound_cue_sound_class"),      MakeHandler(AudioTools::HandleSetSoundCueSoundClassFromArgs));
	D.RegisterHandler(TEXT("set_sound_class_properties"),        MakeHandler(AudioTools::HandleSetSoundClassPropertiesFromArgs));
	D.RegisterHandler(TEXT("set_sound_mix_properties"),          MakeHandler(AudioTools::HandleSetSoundMixPropertiesFromArgs));
	D.RegisterHandler(TEXT("set_audio_submix_properties"),       MakeHandler(AudioTools::HandleSetAudioSubmixPropertiesFromArgs));
	D.RegisterHandler(TEXT("set_sound_concurrency_properties"),  MakeHandler(AudioTools::HandleSetSoundConcurrencyPropertiesFromArgs));
	D.RegisterHandler(TEXT("assign_sound_concurrency"),          MakeHandler(AudioTools::HandleAssignSoundConcurrencyFromArgs));
	D.RegisterHandler(TEXT("get_sound_cue_summary"),             MakeHandler(AudioTools::HandleGetSoundCueSummaryFromArgs));
	D.RegisterHandler(TEXT("set_attenuation_shape"),             MakeHandler(AudioTools::HandleSetAttenuationShapeFromArgs));
	D.RegisterHandler(TEXT("set_attenuation_spatialization"),    MakeHandler(AudioTools::HandleSetAttenuationSpatializationFromArgs));
	D.RegisterHandler(TEXT("add_submix_effect"),                 MakeHandler(AudioTools::HandleAddSubmixEffectFromArgs));
	D.RegisterHandler(TEXT("remove_sound_node"),                 MakeHandler(AudioTools::HandleRemoveSoundNodeFromArgs));
	D.RegisterHandler(TEXT("set_sound_wave_properties"),         MakeHandler(AudioTools::HandleSetSoundWavePropertiesFromArgs));
	D.RegisterHandler(TEXT("get_sound_wave_info"),               MakeHandler(AudioTools::HandleGetSoundWaveInfoFromArgs));
	D.RegisterHandler(TEXT("build_sound_cue_from_spec"),         MakeHandler(AudioTools::HandleBuildSoundCueFromSpecFromArgs));
	D.RegisterHandler(TEXT("set_sound_node_property"),           MakeHandler(AudioTools::HandleSetSoundNodePropertyFromArgs));
	D.RegisterHandler(TEXT("create_quartz_clock"),               MakeHandler(AudioTools::HandleCreateQuartzClock));
	D.RegisterHandler(TEXT("set_quartz_clock_settings"),         MakeHandler(AudioTools::HandleSetQuartzClockSettings));
	D.RegisterHandler(TEXT("get_quartz_clock_info"),             MakeHandler(AudioTools::HandleGetQuartzClockInfo));

	D.RegisterHandler(TEXT("generate_sound"),
		MakeHandler(AudioTools::HandleGenerateSound),
		EUECPToolThreadAffinity::AnyThread);

	D.RegisterHandler(TEXT("get_metasound_summary"),             MakeHandler(MetaSoundTools::HandleGetMetaSoundSummaryFromArgs));
	D.RegisterHandler(TEXT("add_metasound_input"),               MakeHandler(MetaSoundTools::HandleAddMetaSoundInputFromArgs));
	D.RegisterHandler(TEXT("add_metasound_output"),              MakeHandler(MetaSoundTools::HandleAddMetaSoundOutputFromArgs));
	D.RegisterHandler(TEXT("set_metasound_default_parameter"),   MakeHandler(MetaSoundTools::HandleSetMetaSoundDefaultParameterFromArgs));
	D.RegisterHandler(TEXT("duplicate_metasound"),               MakeHandler(MetaSoundTools::HandleDuplicateMetaSoundFromArgs));
	D.RegisterHandler(TEXT("add_metasound_node"),                MakeHandler(MetaSoundTools::HandleAddMetaSoundNodeFromArgs));
	D.RegisterHandler(TEXT("connect_metasound_nodes"),           MakeHandler(MetaSoundTools::HandleConnectMetaSoundNodesFromArgs));
	D.RegisterHandler(TEXT("remove_metasound_node"),             MakeHandler(MetaSoundTools::HandleRemoveMetaSoundNodeFromArgs));
	D.RegisterHandler(TEXT("disconnect_metasound_nodes"),        MakeHandler(MetaSoundTools::HandleDisconnectMetaSoundNodesFromArgs));
	D.RegisterHandler(TEXT("list_metasound_node_types"),         MakeHandler(MetaSoundTools::HandleListMetaSoundNodeTypesFromArgs));
	D.RegisterHandler(TEXT("set_metasound_node_input_default"),  MakeHandler(MetaSoundTools::HandleSetMetaSoundNodeInputDefaultFromArgs));
	D.RegisterHandler(TEXT("remove_metasound_input"),            MakeHandler(MetaSoundTools::HandleRemoveMetaSoundInputFromArgs));
	D.RegisterHandler(TEXT("remove_metasound_output"),           MakeHandler(MetaSoundTools::HandleRemoveMetaSoundOutputFromArgs));
	D.RegisterHandler(TEXT("list_metasound_nodes"),              MakeHandler(MetaSoundTools::HandleListMetaSoundNodesFromArgs));

	{
		const FName U(TEXT("audio"));
		const auto Meta = [&D, &U](const TCHAR* Action, const TCHAR* Summary, const TCHAR* Params)
		{ D.RegisterToolMetadata({ FName(Action), U, FString(Summary), FString(Params) }); };

		Meta(TEXT("generate_sound"),               TEXT("AI-generate a sound (TTS/SFX) via the configured provider (OpenAI/ElevenLabs/Google/Stability/HuggingFace)."), TEXT("text, voice, model, asset_name, save_path, speed"));
		Meta(TEXT("build_sound_cue_from_spec"),    TEXT("Create a complete Sound Cue in one call — waves + nodes + connections + output (+ attenuation). Use instead of add_sound_node x N."), TEXT("asset_path, waves, nodes, connections, output, attenuation?"));
		Meta(TEXT("add_sound_node"),               TEXT("Add a node to a Sound Cue graph."), TEXT("sound_cue_path, node_type=Wave|Random|Mixer|Modulator|Looping|Delay|Attenuation|Enveloper|Concatenator|SwitchInt, position_x/y, sound_wave_path"));
		Meta(TEXT("connect_sound_nodes"),          TEXT("Wire a Sound Cue node into its parent (child feeds parent, flows upward)."), TEXT("sound_cue_path, child_node, parent_node"));
		Meta(TEXT("set_sound_cue_output"),         TEXT("Set a Sound Cue's output node (FirstNode)."), TEXT("sound_cue_path, node_name"));
		Meta(TEXT("remove_sound_node"),            TEXT("Remove a node from a Sound Cue."), TEXT("sound_cue_path, node_name"));
		Meta(TEXT("get_sound_cue_summary"),        TEXT("Summarise a Sound Cue's node graph (read node names before editing)."), TEXT("sound_cue_path"));
		Meta(TEXT("set_sound_node_property"),      TEXT("Set a Sound Cue node property without rebuilding (array_index for per-child Weights/InputVolume)."), TEXT("sound_cue_path, node_name, property_name, property_value, array_index?"));
		Meta(TEXT("set_sound_cue_attenuation"),    TEXT("Assign an attenuation asset to a Sound Cue."), TEXT("sound_cue_path, attenuation_path"));
		Meta(TEXT("set_sound_cue_sound_class"),    TEXT("Route a Sound Cue through a SoundClass (alias assign_sound_cue_sound_class)."), TEXT("sound_cue_path, sound_class_path"));
		Meta(TEXT("assign_sound_cue_sound_class"), TEXT("Alias of set_sound_cue_sound_class."), TEXT("sound_cue_path, sound_class_path"));
		Meta(TEXT("set_sound_class_properties"),   TEXT("Set a SoundClass volume / pitch / LPF."), TEXT("sound_class_path, volume, pitch, lpf_frequency"));
		Meta(TEXT("set_attenuation_shape"),        TEXT("Set an attenuation asset's shape + extents."), TEXT("attenuation_path, shape=Sphere|Capsule|Box|Cone, extents_x/y/z"));
		Meta(TEXT("set_attenuation_spatialization"), TEXT("Set an attenuation asset's spatialization method."), TEXT("attenuation_path, spatialization_method=Default|Binaural|HRTF, enable_binaural"));
		Meta(TEXT("set_sound_mix_properties"),     TEXT("Set a SoundMix's per-class volume/pitch adjusters."), TEXT("sound_mix_path, sound_class_path, volume_adjuster, pitch_adjuster, apply_to_children"));
		Meta(TEXT("set_audio_submix_properties"),  TEXT("Set a submix's volume + parent submix."), TEXT("submix_path, volume, parent_submix_path"));
		Meta(TEXT("add_submix_effect"),            TEXT("Add an effect preset to a submix."), TEXT("submix_path, effect_preset_path"));
		Meta(TEXT("set_sound_concurrency_properties"), TEXT("Configure a SoundConcurrency (max count, resolution policy, fadeout, ...)."), TEXT("asset_path, max_count, resolution_policy, steal_fadeout_time, volume_scale_attenuation, limit_to_owner"));
		Meta(TEXT("assign_sound_concurrency"),     TEXT("Assign a concurrency asset."), TEXT("asset_path, concurrency_path"));
		Meta(TEXT("set_sound_wave_properties"),    TEXT("Set a SoundWave's pitch/volume/loop/loading behaviour + class/attenuation."), TEXT("sound_wave_path, pitch_multiplier, volume_multiplier, loop, loading_behavior, sound_class_path, attenuation_path"));
		Meta(TEXT("get_sound_wave_info"),          TEXT("Get a SoundWave's info."), TEXT("sound_wave_path"));
		Meta(TEXT("set_control_bus_mix_stage"),    TEXT("Set a ControlBusMix stage's target value for a bus (AudioModulation)."), TEXT("mix_path, bus_path, target_value, attack_time, release_time"));
		Meta(TEXT("get_control_bus_summary"),      TEXT("Summarise a SoundControlBus."), TEXT("bus_path"));
		Meta(TEXT("create_quartz_clock"),          TEXT("Create a Quartz clock (requires running PIE)."), TEXT("clock_name, bpm=120, num_beats=4"));
		Meta(TEXT("set_quartz_clock_settings"),    TEXT("Set a Quartz clock's BPM."), TEXT("clock_name, bpm"));
		Meta(TEXT("get_quartz_clock_info"),        TEXT("Get a Quartz clock's info."), TEXT("clock_name"));
	}

	{
		const FName U(TEXT("metasound"));
		const auto Meta = [&D, &U](const TCHAR* Action, const TCHAR* Summary, const TCHAR* Params)
		{ D.RegisterToolMetadata({ FName(Action), U, FString(Summary), FString(Params) }); };

		Meta(TEXT("get_metasound_summary"),        TEXT("Summarise a MetaSound (inputs/outputs/nodes + edge counts). Call before mutating."), TEXT("asset_path"));
		Meta(TEXT("duplicate_metasound"),          TEXT("Duplicate a MetaSound asset."), TEXT("source_path, name, save_path"));
		Meta(TEXT("add_metasound_input"),          TEXT("Add a graph-level input (returns node_id; friendly type aliases resolve, e.g. float/audio/wave)."), TEXT("asset_path, input_name, type_name"));
		Meta(TEXT("add_metasound_output"),         TEXT("Add a graph-level output (default type audio)."), TEXT("asset_path, output_name, type_name"));
		Meta(TEXT("remove_metasound_input"),       TEXT("Remove a graph-level input."), TEXT("asset_path, input_name"));
		Meta(TEXT("remove_metasound_output"),      TEXT("Remove a graph-level output."), TEXT("asset_path, output_name"));
		Meta(TEXT("set_metasound_default_parameter"), TEXT("Set a graph input's default value (parsed into its declared type)."), TEXT("asset_path, parameter_name, value"));
		Meta(TEXT("add_metasound_node"),           TEXT("Add a node by full class path (returns node_id GUID + inputs[]/outputs[] pin names)."), TEXT("asset_path, node_class_name, major_version?"));
		Meta(TEXT("remove_metasound_node"),        TEXT("Remove a node by id."), TEXT("asset_path, node_id"));
		Meta(TEXT("set_metasound_node_input_default"), TEXT("Set a literal default on a specific node pin."), TEXT("asset_path, node_id, input_name, value"));
		Meta(TEXT("connect_metasound_nodes"),      TEXT("Wire one node's output pin to another's input pin (ids are GUIDs; pin names case-sensitive)."), TEXT("asset_path, source_node_id, source_output_name, dest_node_id, dest_input_name"));
		Meta(TEXT("disconnect_metasound_nodes"),   TEXT("Disconnect a node edge."), TEXT("asset_path, source_node_id, source_output_name, dest_node_id, dest_input_name"));
		Meta(TEXT("list_metasound_nodes"),         TEXT("List every node in a MetaSound (class + edge counts)."), TEXT("asset_path"));
		Meta(TEXT("list_metasound_node_types"),    TEXT("Enumerate registered External MetaSound node classes (substring filter, cap 200)."), TEXT("filter?"));
	}

	{
		IUECPCreateAssetRegistry& Reg = IUECPCoreModule::Get().GetCreateAssetRegistry();
		const FName ExtId(TEXT("Audio"));
		Reg.RegisterType(TEXT("SoundCue"),         UECPCreateAsset::FactoryFromArgsFn(&AudioTools::HandleCreateSoundCueFromArgs,         TEXT("SoundCue")),         ExtId);
		Reg.RegisterType(TEXT("SoundAttenuation"), UECPCreateAsset::FactoryFromArgsFn(&AudioTools::HandleCreateSoundAttenuationFromArgs, TEXT("SoundAttenuation")), ExtId);
		Reg.RegisterType(TEXT("SoundClass"),       UECPCreateAsset::FactoryFromArgsFn(&AudioTools::HandleCreateSoundClassFromArgs,       TEXT("SoundClass")),       ExtId);
		Reg.RegisterType(TEXT("SoundMix"),         UECPCreateAsset::FactoryFromArgsFn(&AudioTools::HandleCreateSoundMixFromArgs,         TEXT("SoundMix")),         ExtId);
		Reg.RegisterType(TEXT("SoundConcurrency"), UECPCreateAsset::FactoryFromArgsFn(&AudioTools::HandleCreateSoundConcurrencyFromArgs, TEXT("SoundConcurrency")), ExtId);
		Reg.RegisterType(TEXT("AudioSubmix"),      UECPCreateAsset::FactoryFromArgsFn(&AudioTools::HandleCreateAudioSubmixFromArgs,      TEXT("AudioSubmix")),      ExtId);
		Reg.RegisterType(TEXT("SoundControlBus"),  UECPCreateAsset::FactoryFromArgsFn(&AudioTools::HandleCreateSoundControlBusFromArgs,  TEXT("SoundControlBus")),  ExtId);
		Reg.RegisterType(TEXT("ControlBusMix"),    UECPCreateAsset::FactoryFromArgsFn(&AudioTools::HandleCreateControlBusMixFromArgs,    TEXT("ControlBusMix")),    ExtId);
		Reg.RegisterType(TEXT("DialogueVoice"),    UECPCreateAsset::FactoryFromArgsFn(&AudioTools::HandleCreateDialogueVoiceFromArgs,    TEXT("DialogueVoice")),    ExtId);
		Reg.RegisterType(TEXT("DialogueWave"),     UECPCreateAsset::FactoryFromArgsFn(&AudioTools::HandleCreateDialogueWaveFromArgs,     TEXT("DialogueWave")),     ExtId);
		Reg.RegisterType(TEXT("MetaSound"),        UECPCreateAsset::FactoryFromArgsFn(&MetaSoundTools::HandleCreateMetaSoundFromArgs,    TEXT("MetaSound")),        ExtId);
	}

	UE_LOG(LogUECPAudioExt, Log, TEXT("Registered %d Audio tools (audio + metasound umbrellas)"),
		OwnedToolNames().Num());
}

void FUECPAudioExtModule::ShutdownModule()
{
	if (!IUECPCoreModule::IsAvailable()) return;
	IUECPToolDispatcher& D = IUECPCoreModule::Get().GetToolDispatcher();
	for (const FName& N : OwnedToolNames()) D.UnregisterHandler(N);
	IUECPCreateAssetRegistry& Reg = IUECPCoreModule::Get().GetCreateAssetRegistry();
	for (const TCHAR* T : { TEXT("SoundCue"), TEXT("SoundAttenuation"), TEXT("SoundClass"),
	                        TEXT("SoundMix"), TEXT("SoundConcurrency"), TEXT("AudioSubmix"),
	                        TEXT("SoundControlBus"), TEXT("ControlBusMix"),
	                        TEXT("DialogueVoice"), TEXT("DialogueWave"), TEXT("MetaSound") })
	{
		Reg.UnregisterType(T);
	}
}

IMPLEMENT_MODULE(FUECPAudioExtModule, UECPAudioExt)
