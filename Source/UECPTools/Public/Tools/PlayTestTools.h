// Copyright 2026, BlueprintsLab, All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

namespace PlayTestTools
{

	UECPTOOLS_API void HandleGetPieStatus(FString& OutJsonString, FString& OutError);

	UECPTOOLS_API void HandleGetPieLog(int32 LineCount, const FString& CategoryFilter, const FString& SeverityFilter,
		FString& OutJsonString, FString& OutError);

	UECPTOOLS_API void HandleGetPieActors(const FString& ClassFilter, int32 MaxCount,
		FString& OutJsonString, FString& OutError);

	UECPTOOLS_API void HandleGetPieActorState(const FString& ActorLabel, const TArray<FString>& PropertyNames,
		FString& OutJsonString, FString& OutError);

	UECPTOOLS_API void HandleSimulateInput(const FString& Key, const FString& EventType, float Duration,
		FString& OutJsonString, FString& OutError);

	UECPTOOLS_API void HandleExecutePieConsoleCommand(const FString& Command,
		FString& OutJsonString, FString& OutError);

	UECPTOOLS_API void HandleTakePieScreenshot(const FString& FilePath,
		FString& OutJsonString, FString& OutError);

	UECPTOOLS_API void HandleGetPiePerformance(FString& OutJsonString, FString& OutError);

	UECPTOOLS_API void HandleSetPieActorProperty(const FString& ActorLabel, const FString& PropertyName, const FString& PropertyValue,
		FString& OutJsonString, FString& OutError);

	UECPTOOLS_API void HandlePieLineTrace(const FString& StartStr, const FString& EndStr, const FString& Channel,
		FString& OutJsonString, FString& OutError);

	UECPTOOLS_API void HandleGetPiePlayerState(const TArray<FString>& PropertyNames,
		FString& OutJsonString, FString& OutError);

	UECPTOOLS_API void HandleWaitForPieEvent(const FString& ConditionType, const FString& ActorLabel,
		const FString& PropertyName, const FString& ExpectedValue, float TimeoutSeconds,
		FString& OutJsonString, FString& OutError);

	UECPTOOLS_API void HandleCallPieBlueprintFunction(const FString& ActorLabel, const FString& FunctionName,
		const TSharedPtr<FJsonObject>& Params, FString& OutJsonString, FString& OutError);

	UECPTOOLS_API void HandleRunPieTestSequence(const FString& StepsJson, bool bStopOnFailure,
		FString& OutJsonString, FString& OutError);

	UECPTOOLS_API void HandlePausePie(bool bPause, FString& OutJsonString, FString& OutError);

	UECPTOOLS_API void HandleSetTimeDilation(float Dilation, FString& OutJsonString, FString& OutError);

	UECPTOOLS_API void HandleLookAtTarget(float MaxDistance, const FString& Channel,
		FString& OutJsonString, FString& OutError);

	UECPTOOLS_API void HandleIsPlayerStuck(float WindowSeconds, float MoveThreshold,
		FString& OutJsonString, FString& OutError);

	UECPTOOLS_API void HandleSimulateMouseDelta(float DeltaX, float DeltaY,
		FString& OutJsonString, FString& OutError);

	UECPTOOLS_API void HandleSimulateInputAxis(const FString& InputActionPath,
		float ValueX, float ValueY, float ValueZ, FString& OutJsonString, FString& OutError);

	UECPTOOLS_API void HandleSimulateInputBurst(const TArray<TSharedPtr<FJsonValue>>& Steps,
		FString& OutJsonString, FString& OutError);

	UECPTOOLS_API void HandleGetVisibleWidgets(int32 MaxWidgets, FString& OutJsonString, FString& OutError);

	UECPTOOLS_API void HandleActorsNearPlayer(float Radius, const FString& ClassFilter,
		FString& OutJsonString, FString& OutError);

	UECPTOOLS_API void HandleGetPlayerRuntimeState(FString& OutJsonString, FString& OutError);

	UECPTOOLS_API void HandleTeleportPlayer(const FString& LocationStr, const FString& RotationStr,
		FString& OutJsonString, FString& OutError);

	UECPTOOLS_API void HandleSpawnTestActor(const FString& ClassPath, const FString& LocationStr,
		const FString& RotationStr, FString& OutJsonString, FString& OutError);

	UECPTOOLS_API void HandleGetGasAttributes(const FString& ActorLabel,
		FString& OutJsonString, FString& OutError);

	UECPTOOLS_API void HandleGenerateTestReport(const FString& Title, const FString& OutputPath,
		const TSharedPtr<FJsonObject>& Sections, FString& OutJsonString, FString& OutError);

	UECPTOOLS_API void HandlePausePieFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPTOOLS_API void HandleSetTimeDilationFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPTOOLS_API void HandleLookAtTargetFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPTOOLS_API void HandleIsPlayerStuckFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPTOOLS_API void HandleSimulateMouseDeltaFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPTOOLS_API void HandleSimulateInputAxisFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPTOOLS_API void HandleSimulateInputBurstFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPTOOLS_API void HandleGetVisibleWidgetsFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPTOOLS_API void HandleActorsNearPlayerFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPTOOLS_API void HandleGetPlayerRuntimeStateFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPTOOLS_API void HandleTeleportPlayerFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPTOOLS_API void HandleSpawnTestActorFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPTOOLS_API void HandleGetGasAttributesFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPTOOLS_API void HandleGenerateTestReportFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);

	UECPTOOLS_API void HandleGetPieStatusFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPTOOLS_API void HandleGetPieLogFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPTOOLS_API void HandleGetPieActorsFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPTOOLS_API void HandleGetPieActorStateFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPTOOLS_API void HandleSimulateInputFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPTOOLS_API void HandleExecutePieConsoleCommandFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPTOOLS_API void HandleTakePieScreenshotFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPTOOLS_API void HandleGetPiePerformanceFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPTOOLS_API void HandleSetPieActorPropertyFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPTOOLS_API void HandlePieLineTraceFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPTOOLS_API void HandleGetPiePlayerStateFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPTOOLS_API void HandleWaitForPieEventFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPTOOLS_API void HandleCallPieBlueprintFunctionFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPTOOLS_API void HandleRunPieTestSequenceFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
}
