// Copyright 2026, BlueprintsLab, All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

namespace NavMeshTools
{

	UECPAIEXT_API void HandleSpawnNavMeshBoundsVolume(const FString& ActorLabel,
		float LocationX, float LocationY, float LocationZ,
		float ExtentX, float ExtentY, float ExtentZ,
		FString& OutJsonString, FString& OutError);

	UECPAIEXT_API void HandleSetNavMeshConfig(float AgentRadius, float AgentHeight, float MaxStepHeight, float CellSize,
		FString& OutJsonString, FString& OutError);

	UECPAIEXT_API void HandleRebuildNavMesh(FString& OutJsonString, FString& OutError);

	UECPAIEXT_API void HandleGetNavMeshInfo(FString& OutJsonString, FString& OutError);

	UECPAIEXT_API void HandleAddAIPerceptionComponent(const FString& BlueprintPath, const FString& ComponentName,
		FString& OutJsonString, FString& OutError);

	UECPAIEXT_API void HandleConfigureAISight(const FString& BlueprintPath,
		float SightRadius, float LoseSightRadius, float PeripheralAngle,
		bool bDetectEnemies, bool bDetectNeutrals, bool bDetectFriendlies,
		FString& OutJsonString, FString& OutError);

	UECPAIEXT_API void HandleConfigureAIHearing(const FString& BlueprintPath, float HearingRange,
		bool bDetectEnemies, bool bDetectNeutrals, bool bDetectFriendlies,
		FString& OutJsonString, FString& OutError);

	UECPAIEXT_API void HandleAddNavModifierVolume(const FString& ActorLabel,
		float LocationX, float LocationY, float LocationZ,
		float ExtentX, float ExtentY, float ExtentZ,
		const FString& AreaClass, FString& OutJsonString, FString& OutError);

	UECPAIEXT_API void HandleAddNavLinkProxy(const FString& ActorLabel,
		float LocationX, float LocationY, float LocationZ,
		float EndOffsetX, float EndOffsetY, float EndOffsetZ,
		FString& OutJsonString, FString& OutError);

	UECPAIEXT_API void HandleSetAIControllerClass(const FString& BlueprintPath, const FString& AIControllerClass,
		FString& OutJsonString, FString& OutError);

	UECPAIEXT_API void HandleConfigureAIDamage(const FString& BlueprintPath,
		bool bDetectEnemies, bool bDetectNeutrals, bool bDetectFriendlies,
		FString& OutJsonString, FString& OutError);

	UECPAIEXT_API void HandleCreateSmartObjectDefinition(const FString& Name, const FString& SavePath,
		FString& OutJsonString, FString& OutError);

	UECPAIEXT_API void HandleAddSmartObjectSlot(const FString& AssetPath, const FString& ActivityTagName,
		FString& OutJsonString, FString& OutError);

	UECPAIEXT_API void HandleGetSmartObjectInfo(const FString& AssetPath, FString& OutJsonString, FString& OutError);

	UECPAIEXT_API void HandleSpawnNavMeshBoundsVolumeFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPAIEXT_API void HandleSetNavMeshConfigFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPAIEXT_API void HandleRebuildNavMeshFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPAIEXT_API void HandleGetNavMeshInfoFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPAIEXT_API void HandleAddAIPerceptionComponentFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPAIEXT_API void HandleConfigureAISightFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPAIEXT_API void HandleConfigureAIHearingFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPAIEXT_API void HandleAddNavModifierVolumeFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPAIEXT_API void HandleAddNavLinkProxyFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPAIEXT_API void HandleSetAIControllerClassFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPAIEXT_API void HandleConfigureAIDamageFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPAIEXT_API void HandleCreateSmartObjectDefinitionFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPAIEXT_API void HandleAddSmartObjectSlotFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPAIEXT_API void HandleGetSmartObjectInfoFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
}
