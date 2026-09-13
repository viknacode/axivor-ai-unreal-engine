// Copyright 2026, BlueprintsLab, All rights reserved

#include "UECPVehicleExtModule.h"
#include "Tools/VehicleTools.h"
#include "UECPCoreModule.h"
#include "Services/IUECPToolDispatcher.h"
#include "Services/IUECPCreateAssetRegistry.h"

DEFINE_LOG_CATEGORY(LogUECPVehicleExt);

namespace
{
	static const TArray<FName>& OwnedToolNames()
	{
		static const TArray<FName> Names = {
			TEXT("add_vehicle_wheel"),
			TEXT("set_vehicle_engine_config"),
			TEXT("set_vehicle_transmission_config"),
			TEXT("set_vehicle_suspension_config"),
			TEXT("set_vehicle_steering_config"),
			TEXT("get_vehicle_config_summary"),
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

void FUECPVehicleExtModule::StartupModule()
{
	if (!IUECPCoreModule::IsAvailable()) return;
	IUECPToolDispatcher& D = IUECPCoreModule::Get().GetToolDispatcher();

	D.RegisterHandler(TEXT("add_vehicle_wheel"),              MakeHandler(VehicleTools::HandleAddVehicleWheelFromArgs));
	D.RegisterHandler(TEXT("set_vehicle_engine_config"),      MakeHandler(VehicleTools::HandleSetVehicleEngineConfigFromArgs));
	D.RegisterHandler(TEXT("set_vehicle_transmission_config"),MakeHandler(VehicleTools::HandleSetVehicleTransmissionConfigFromArgs));
	D.RegisterHandler(TEXT("set_vehicle_suspension_config"),  MakeHandler(VehicleTools::HandleSetVehicleSuspensionConfigFromArgs));
	D.RegisterHandler(TEXT("set_vehicle_steering_config"),    MakeHandler(VehicleTools::HandleSetVehicleSteeringConfigFromArgs));
	D.RegisterHandler(TEXT("get_vehicle_config_summary"),     MakeHandler(VehicleTools::HandleGetVehicleConfigSummaryFromArgs));

	IUECPCoreModule::Get().GetCreateAssetRegistry().RegisterType(TEXT("VehicleBlueprint"),
		UECPCreateAsset::FactoryFromArgsFn(&VehicleTools::HandleCreateVehicleBlueprintFromArgs, TEXT("VehicleBlueprint")),
		TEXT("Vehicle"));

	{
		const FName U(TEXT("vehicle"));
		const auto Meta = [&D, &U](const TCHAR* Action, const TCHAR* Summary, const TCHAR* Params)
		{ D.RegisterToolMetadata({ FName(Action), U, FString(Summary), FString(Params) }); };

		Meta(TEXT("add_vehicle_wheel"),              TEXT("Add a wheel to a Chaos vehicle Blueprint."), TEXT("blueprint_path, bone_name, wheel_class, additional_offset_x/y/z?"));
		Meta(TEXT("set_vehicle_engine_config"),      TEXT("Set engine torque/rpm/idle."), TEXT("blueprint_path, max_torque?, max_rpm?, engine_idle_rpm?"));
		Meta(TEXT("set_vehicle_transmission_config"),TEXT("Set gearbox ratios/shift rpms."), TEXT("blueprint_path, automatic?, forward_gear_ratios?, reverse_gear_ratios?, final_ratio?, change_up_rpm?, change_down_rpm?"));
		Meta(TEXT("set_vehicle_suspension_config"),  TEXT("Set suspension/wheel radius/steer (edits wheel BP CDO)."), TEXT("wheel_class_path, suspension_max_raise?, suspension_max_drop?, suspension_damping_ratio?, wheel_radius?, max_steer_angle?"));
		Meta(TEXT("set_vehicle_steering_config"),    TEXT("Set max steer angle."), TEXT("blueprint_path, max_steer_angle"));
		Meta(TEXT("get_vehicle_config_summary"),     TEXT("Summarise a vehicle Blueprint config."), TEXT("blueprint_path"));
	}

	UE_LOG(LogUECPVehicleExt, Log, TEXT("Registered %d Vehicle tools"), OwnedToolNames().Num());
}

void FUECPVehicleExtModule::ShutdownModule()
{
	if (!IUECPCoreModule::IsAvailable()) return;
	IUECPToolDispatcher& D = IUECPCoreModule::Get().GetToolDispatcher();
	for (const FName& N : OwnedToolNames()) D.UnregisterHandler(N);
	IUECPCoreModule::Get().GetCreateAssetRegistry().UnregisterType(TEXT("VehicleBlueprint"));
}

IMPLEMENT_MODULE(FUECPVehicleExtModule, UECPVehicleExt)
