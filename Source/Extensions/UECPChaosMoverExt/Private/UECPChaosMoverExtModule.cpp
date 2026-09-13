// Copyright 2026, BlueprintsLab, All rights reserved

#include "UECPChaosMoverExtModule.h"
#include "Tools/ChaosMoverTools.h"
#include "UECPCoreModule.h"
#include "Services/IUECPToolDispatcher.h"

DEFINE_LOG_CATEGORY(LogUECPChaosMoverExt);

namespace
{
	static const TArray<FName>& OwnedToolNames()
	{
		static const TArray<FName> Names = {
			TEXT("setup_chaos_mover_character"),
			TEXT("configure_chaos_mover_settings"),
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

void FUECPChaosMoverExtModule::StartupModule()
{
	if (!IUECPCoreModule::IsAvailable()) return;
	IUECPToolDispatcher& D = IUECPCoreModule::Get().GetToolDispatcher();

	D.RegisterHandler(TEXT("setup_chaos_mover_character"),    MakeHandler(ChaosMoverTools::HandleSetupChaosMoverCharacterFromArgs));
	D.RegisterHandler(TEXT("configure_chaos_mover_settings"), MakeHandler(ChaosMoverTools::HandleConfigureChaosMoverSettingsFromArgs));

	{
		const FName U(TEXT("chaos_mover"));
		const auto Meta = [&D, &U](const TCHAR* Action, const TCHAR* Summary, const TCHAR* Params)
		{ D.RegisterToolMetadata({ FName(Action), U, FString(Summary), FString(Params) }); };

		Meta(TEXT("setup_chaos_mover_character"),    TEXT("Create a physics-based Chaos Mover character (UChaosCharacterMoverComponent; auto Chaos backend + Walking/Falling/Flying). On a fresh BP also scaffolds a functional character (capsule physics body + input producer + ProduceInput graph) and enables the project physics prerequisites (reports restart_required). Add/remove modes + per-mode physics via the 'mover' umbrella."), TEXT("blueprint_path|(name+save_path), parent_class?, modes?, starting_mode?, scaffold_input?"));
		Meta(TEXT("configure_chaos_mover_settings"), TEXT("Set SharedChaosCharacterMovementSettings (speed/accel/friction/turning/slope) on a Chaos mover BP."), TEXT("blueprint_path, max_speed?, acceleration?, deceleration?, turning_rate?, turning_boost?, ground_friction?, braking_friction?, braking_friction_factor?, max_step_height?, max_walkable_slope_angle?, use_separate_braking_friction?, use_acceleration_for_velocity_move?, default_falling_mode?"));
	}

	UE_LOG(LogUECPChaosMoverExt, Log, TEXT("Registered %d Chaos Mover tools"), OwnedToolNames().Num());
}

void FUECPChaosMoverExtModule::ShutdownModule()
{
	if (!IUECPCoreModule::IsAvailable()) return;
	IUECPToolDispatcher& D = IUECPCoreModule::Get().GetToolDispatcher();
	for (const FName& N : OwnedToolNames()) D.UnregisterHandler(N);
}

IMPLEMENT_MODULE(FUECPChaosMoverExtModule, UECPChaosMoverExt)
