// Copyright 2026, BlueprintsLab, All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

namespace CurveTools
{

	UECPTOOLS_API void HandleCreateCurveFloat(const FString& Name, const FString& SavePath,
		const TArray<TPair<float,float>>& Keys, FString& OutJsonString, FString& OutError);

	UECPTOOLS_API void HandleCreateCurveVector(const FString& Name, const FString& SavePath,
		FString& OutJsonString, FString& OutError);

	UECPTOOLS_API void HandleCreateCurveLinearColor(const FString& Name, const FString& SavePath,
		FString& OutJsonString, FString& OutError);

	UECPTOOLS_API void HandleAddCurveKey(const FString& AssetPath, float Time, float Value,
		int32 Channel, const FString& InterpMode, FString& OutJsonString, FString& OutError);

	UECPTOOLS_API void HandleGetCurveKeys(const FString& AssetPath, int32 Channel,
		FString& OutJsonString, FString& OutError);

	UECPTOOLS_API void HandleRemoveCurveKey(const FString& AssetPath, float Time, int32 Channel,
		FString& OutJsonString, FString& OutError);

	UECPTOOLS_API void HandleSetCurveKeyInterp(const FString& AssetPath, float Time, int32 Channel,
		const FString& InterpMode, FString& OutJsonString, FString& OutError);

	UECPTOOLS_API void HandleSetCurveKeyTangent(const FString& AssetPath, float Time, int32 Channel,
		float ArriveTangent, float LeaveTangent, FString& OutJsonString, FString& OutError);

	UECPTOOLS_API void HandleAddCurveKeyFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPTOOLS_API void HandleRemoveCurveKeyFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPTOOLS_API void HandleCreateCurveFloatFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPTOOLS_API void HandleCreateCurveVectorFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPTOOLS_API void HandleCreateCurveLinearColorFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPTOOLS_API void HandleGetCurveKeysFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPTOOLS_API void HandleSetCurveKeyInterpFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPTOOLS_API void HandleSetCurveKeyTangentFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
}
