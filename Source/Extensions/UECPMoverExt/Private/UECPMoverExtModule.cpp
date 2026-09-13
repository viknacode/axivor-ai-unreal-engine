// Copyright 2026, BlueprintsLab, All rights reserved

#include "UECPMoverExtModule.h"
#include "Tools/MoverTools.h"
#include "UECPCoreModule.h"
#include "Services/IUECPToolDispatcher.h"

DEFINE_LOG_CATEGORY(LogUECPMoverExt);

namespace
{
	static const TArray<FName>& OwnedToolNames()
	{
		static const TArray<FName> Names = {
			TEXT("setup_mover_character"),
			TEXT("configure_mover_settings"),
			TEXT("configure_stance_settings"),
			TEXT("add_movement_mode"),
			TEXT("remove_movement_mode"),
			TEXT("set_starting_movement_mode"),
			TEXT("configure_mover_component"),
			TEXT("get_mover_summary"),
			TEXT("configure_movement_mode"),
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

void FUECPMoverExtModule::StartupModule()
{
	if (!IUECPCoreModule::IsAvailable()) return;
	IUECPToolDispatcher& D = IUECPCoreModule::Get().GetToolDispatcher();

	D.RegisterHandler(TEXT("setup_mover_character"),       MakeHandler(MoverTools::HandleSetupMoverCharacterFromArgs));
	D.RegisterHandler(TEXT("configure_mover_settings"),    MakeHandler(MoverTools::HandleConfigureMoverSettingsFromArgs));
	D.RegisterHandler(TEXT("configure_stance_settings"),   MakeHandler(MoverTools::HandleConfigureStanceSettingsFromArgs));
	D.RegisterHandler(TEXT("add_movement_mode"),           MakeHandler(MoverTools::HandleAddMovementModeFromArgs));
	D.RegisterHandler(TEXT("remove_movement_mode"),        MakeHandler(MoverTools::HandleRemoveMovementModeFromArgs));
	D.RegisterHandler(TEXT("set_starting_movement_mode"),  MakeHandler(MoverTools::HandleSetStartingMovementModeFromArgs));
	D.RegisterHandler(TEXT("configure_mover_component"),   MakeHandler(MoverTools::HandleConfigureMoverComponentFromArgs));
	D.RegisterHandler(TEXT("get_mover_summary"),           MakeHandler(MoverTools::HandleGetMoverSummaryFromArgs));
	D.RegisterHandler(TEXT("configure_movement_mode"),     MakeHandler(MoverTools::HandleConfigureMovementModeFromArgs));

	{
		const FName U(TEXT("mover"));
		const auto Meta = [&D, &U](const TCHAR* Action, const TCHAR* Summary, const TCHAR* Params)
		{ D.RegisterToolMetadata({ FName(Action), U, FString(Summary), FString(Params) }); };

		Meta(TEXT("setup_mover_character"),     TEXT("Create/configure a Mover 2.0 character Blueprint."), TEXT("blueprint_path|(name+save_path), parent_class?, modes?, starting_mode?"));
		Meta(TEXT("configure_mover_settings"),  TEXT("Set speed/accel/friction/jump/step movement settings."), TEXT("blueprint_path, max_speed?, acceleration?, deceleration?, jump_speed?, ground_friction?, max_step_height?, ..."));
		Meta(TEXT("configure_stance_settings"), TEXT("Set crouch speed/accel/half-height/eye-height."), TEXT("blueprint_path, crouch_max_speed?, crouch_max_acceleration?, crouch_half_height?, crouched_eye_height?"));
		Meta(TEXT("configure_mover_component"), TEXT("Set smoothing/gravity/constraint on the mover component."), TEXT("blueprint_path, smoothing_mode?, gravity_override?, gravity_vector?, planar_constraint_enabled?, ..."));
		Meta(TEXT("add_movement_mode"),         TEXT("Add a movement mode (batch)."), TEXT("blueprint_path, mode_name, mode_class"));
		Meta(TEXT("remove_movement_mode"),      TEXT("Remove a movement mode (batch)."), TEXT("blueprint_path, mode_name"));
		Meta(TEXT("set_starting_movement_mode"),TEXT("Set the starting movement mode."), TEXT("blueprint_path, starting_mode"));
		Meta(TEXT("get_mover_summary"),         TEXT("Summarise a Mover character Blueprint (modes + per-mode props + settings; reports is_chaos)."), TEXT("blueprint_path"));
		Meta(TEXT("configure_movement_mode"),   TEXT("Set per-mode properties on one mode (standard Falling air-control props AND Chaos constraint/walking/swimming physics). Reports applied vs unsupported_for_mode."), TEXT("blueprint_path, mode_name, [air_control, falling_deceleration, terminal_vertical_speed, cancel_vertical_speed_on_landing, radial_force_limit, twist_torque_limit, swing_torque_limit, remain_upright, target_height, query_radius, ground_damping, friction_force_limit, fractional_ground_reaction, swimming_ideal_immersion_depth, ...]"));
	}

	UE_LOG(LogUECPMoverExt, Log, TEXT("Registered %d Mover tools"), OwnedToolNames().Num());
}

void FUECPMoverExtModule::ShutdownModule()
{
	if (!IUECPCoreModule::IsAvailable()) return;
	IUECPToolDispatcher& D = IUECPCoreModule::Get().GetToolDispatcher();
	for (const FName& N : OwnedToolNames()) D.UnregisterHandler(N);
}

IMPLEMENT_MODULE(FUECPMoverExtModule, UECPMoverExt)
