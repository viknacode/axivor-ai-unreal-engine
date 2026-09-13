// Copyright 2026, BlueprintsLab, All rights reserved

#include "UECPWaterExtModule.h"
#include "Tools/WaterTools.h"
#include "UECPCoreModule.h"
#include "Services/IUECPToolDispatcher.h"

DEFINE_LOG_CATEGORY(LogUECPWaterExt);

namespace
{
	static const TArray<FName>& OwnedToolNames()
	{
		static const TArray<FName> Names = {
			TEXT("create_water_body_river"),
			TEXT("create_water_body_lake"),
			TEXT("create_water_body_ocean"),
			TEXT("set_water_material"),
			TEXT("set_water_wave_settings"),
			TEXT("get_water_body_info"),
			TEXT("set_water_body_properties"),
			TEXT("clear_water_waves"),
			TEXT("set_water_body_spline_points"),
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

void FUECPWaterExtModule::StartupModule()
{
	if (!IUECPCoreModule::IsAvailable()) return;
	IUECPToolDispatcher& D = IUECPCoreModule::Get().GetToolDispatcher();

	D.RegisterHandler(TEXT("create_water_body_river"),    MakeHandler(WaterTools::HandleCreateWaterBodyRiverFromArgs));
	D.RegisterHandler(TEXT("create_water_body_lake"),     MakeHandler(WaterTools::HandleCreateWaterBodyLakeFromArgs));
	D.RegisterHandler(TEXT("create_water_body_ocean"),    MakeHandler(WaterTools::HandleCreateWaterBodyOceanFromArgs));
	D.RegisterHandler(TEXT("set_water_material"),         MakeHandler(WaterTools::HandleSetWaterMaterialFromArgs));
	D.RegisterHandler(TEXT("set_water_wave_settings"),    MakeHandler(WaterTools::HandleSetWaterWaveSettingsFromArgs));
	D.RegisterHandler(TEXT("get_water_body_info"),        MakeHandler(WaterTools::HandleGetWaterBodyInfoFromArgs));
	D.RegisterHandler(TEXT("set_water_body_properties"),  MakeHandler(WaterTools::HandleSetWaterBodyPropertiesFromArgs));
	D.RegisterHandler(TEXT("clear_water_waves"),          MakeHandler(WaterTools::HandleClearWaterWavesFromArgs));
	D.RegisterHandler(TEXT("set_water_body_spline_points"), MakeHandler(WaterTools::HandleSetWaterBodySplinePointsFromArgs));

	{
		const FName U(TEXT("water"));
		const auto Meta = [&D, &U](const TCHAR* Action, const TCHAR* Summary, const TCHAR* Params)
		{ D.RegisterToolMetadata({ FName(Action), U, FString(Summary), FString(Params) }); };

		Meta(TEXT("create_water_body_river"),    TEXT("Create a river water body from a spline path (first point = spawn location)."), TEXT("actor_label, spline_points=[[x,y,z]]"));
		Meta(TEXT("create_water_body_lake"),     TEXT("Create a lake (auto 8-point closed circular spline at radius)."), TEXT("actor_label, location_x/y/z, radius=2000"));
		Meta(TEXT("create_water_body_ocean"),    TEXT("Create an ocean (no shape spline — covers the world; WaterZone handles bounds)."), TEXT("actor_label, location_x/y/z"));
		Meta(TEXT("set_water_body_spline_points"), TEXT("Replace a river/lake spline path (closed_loop=true for lakes, false for rivers; ocean errors)."), TEXT("actor_label, spline_points=[[x,y,z]], closed_loop?"));
		Meta(TEXT("set_water_body_properties"),  TEXT("Generic FProperty setter on the water actor/component (bAffectsLandscape, bGenerateCollisions, WaterZoneActor, ...)."), TEXT("actor_label, property_name, property_value"));
		Meta(TEXT("set_water_material"),         TEXT("Set a water body's material (SetWaterMaterial; falls back to Material[0])."), TEXT("actor_label, material_path"));
		Meta(TEXT("get_water_body_info"),        TEXT("Get a water body's class/type/location/material/wave_count (+ spline info for rivers)."), TEXT("actor_label"));
		Meta(TEXT("set_water_wave_settings"),    TEXT("Add Gerstner waves (single fields or waves=[...] batch). Pass clear_existing=true to replace — otherwise calls accumulate."), TEXT("actor_label, amplitude/wave_length/direction_deg/steepness | waves=[...], clear_existing?"));
		Meta(TEXT("clear_water_waves"),          TEXT("Remove every Gerstner wave from a water body."), TEXT("actor_label"));
	}

	UE_LOG(LogUECPWaterExt, Log, TEXT("Registered %d Water tools"), OwnedToolNames().Num());
}

void FUECPWaterExtModule::ShutdownModule()
{
	if (!IUECPCoreModule::IsAvailable()) return;
	IUECPToolDispatcher& D = IUECPCoreModule::Get().GetToolDispatcher();
	for (const FName& N : OwnedToolNames()) D.UnregisterHandler(N);
}

IMPLEMENT_MODULE(FUECPWaterExtModule, UECPWaterExt)
