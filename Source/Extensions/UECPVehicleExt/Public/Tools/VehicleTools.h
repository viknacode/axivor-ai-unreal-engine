// Copyright 2026, BlueprintsLab, All rights reserved.

#pragma once
#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

namespace VehicleTools
{

	UECPVEHICLEEXT_API void HandleCreateVehicleBlueprint(const FString& Name, const FString& SavePath,
		FString& OutJsonString, FString& OutError);

	UECPVEHICLEEXT_API void HandleAddVehicleWheel(const FString& BlueprintPath, const FString& WheelClass,
		const FString& BoneName, float OffsetX, float OffsetY, float OffsetZ,
		FString& OutJsonString, FString& OutError);

	UECPVEHICLEEXT_API void HandleSetVehicleEngineConfig(const FString& BlueprintPath,
		float MaxTorque, float MaxRPM, float EngineIdleRPM,
		FString& OutJsonString, FString& OutError);

	UECPVEHICLEEXT_API void HandleSetVehicleTransmissionConfig(const FString& BlueprintPath,
		bool bAutomatic, bool bAutomaticSet,
		const TArray<float>& ForwardGearRatios, const TArray<float>& ReverseGearRatios,
		float FinalRatio, float ChangeUpRPM, float ChangeDownRPM,
		FString& OutJsonString, FString& OutError);

	UECPVEHICLEEXT_API void HandleSetVehicleSuspensionConfig(const FString& WheelClassPath,
		float MaxRaise, float MaxDrop, float DampingRatio, float WheelRadius, float MaxSteerAngle,
		FString& OutJsonString, FString& OutError);

	UECPVEHICLEEXT_API void HandleSetVehicleSteeringConfig(const FString& BlueprintPath, float MaxSteerAngle,
		FString& OutJsonString, FString& OutError);

	UECPVEHICLEEXT_API void HandleGetVehicleConfigSummary(const FString& BlueprintPath,
		FString& OutJsonString, FString& OutError);

	UECPVEHICLEEXT_API void HandleAddVehicleWheelFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPVEHICLEEXT_API void HandleCreateVehicleBlueprintFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPVEHICLEEXT_API void HandleSetVehicleEngineConfigFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPVEHICLEEXT_API void HandleSetVehicleTransmissionConfigFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPVEHICLEEXT_API void HandleSetVehicleSuspensionConfigFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPVEHICLEEXT_API void HandleSetVehicleSteeringConfigFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPVEHICLEEXT_API void HandleGetVehicleConfigSummaryFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
}
