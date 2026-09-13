// Copyright 2026, BlueprintsLab, All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

namespace PoseSearchTools
{
	UECPANIMATIONEXT_API void HandleCreatePoseSearchSchema(const FString& AssetName, const FString& SavePath,
		const FString& SkeletonPath, int32 SampleRate, FString& OutJson, FString& OutError);

	UECPANIMATIONEXT_API void HandleAddPoseSearchChannel(const FString& SchemaPath, const FString& ChannelType,
		const TSharedPtr<FJsonObject>& Params, FString& OutJson, FString& OutError);

	UECPANIMATIONEXT_API void HandleCreatePoseSearchDatabase(const FString& AssetName, const FString& SavePath,
		const FString& SchemaPath, FString& OutJson, FString& OutError);

	UECPANIMATIONEXT_API void HandleAddAnimationToDatabase(const FString& DatabasePath, const FString& AnimationType,
		const FString& AnimationPath, FString& OutJson, FString& OutError);

	UECPANIMATIONEXT_API void HandleBuildPoseSearchDatabase(const FString& DatabasePath, FString& OutJson, FString& OutError);

	UECPANIMATIONEXT_API void HandleGetPoseSearchSummary(const FString& AssetPath, FString& OutJson, FString& OutError);

	UECPANIMATIONEXT_API void HandleSetPoseSearchChannelProperty(const FString& SchemaPath, int32 ChannelIndex,
		const FString& PropertyName, const FString& PropertyValue, FString& OutJson, FString& OutError);

	UECPANIMATIONEXT_API void HandleSetPoseSearchSchemaSkeleton(const FString& SchemaPath, const FString& SkeletonPath,
		int32 SkeletonIndex, const FString& Role, FString& OutJson, FString& OutError);

	UECPANIMATIONEXT_API void HandleRemoveAnimationFromDatabase(const FString& DatabasePath, int32 AnimationIndex,
		FString& OutJson, FString& OutError);

	UECPANIMATIONEXT_API void HandleSetPoseSearchSchemaProperty(const FString& SchemaPath, const FString& PropertyName,
		const FString& PropertyValue, FString& OutJson, FString& OutError);

	UECPANIMATIONEXT_API void HandleRemovePoseSearchChannel(const FString& SchemaPath, int32 ChannelIndex,
		FString& OutJson, FString& OutError);

	UECPANIMATIONEXT_API void HandleSetAnimationDatabaseEntryProperty(const FString& DatabasePath, int32 AnimationIndex,
		const FString& PropertyName, const FString& PropertyValue, FString& OutJson, FString& OutError);

	UECPANIMATIONEXT_API void HandleSetPoseSearchDatabaseSchema(const FString& DatabasePath, const FString& SchemaPath,
		FString& OutJson, FString& OutError);

	UECPANIMATIONEXT_API void HandleGetPoseSearchChannelProperties(const FString& SchemaPath, int32 ChannelIndex,
		FString& OutJson, FString& OutError);

	UECPANIMATIONEXT_API void HandleCreatePoseSearchNormalizationSet(const FString& AssetName, const FString& SavePath,
		FString& OutJson, FString& OutError);

	UECPANIMATIONEXT_API void HandleAddDatabaseToNormalizationSet(const FString& NormSetPath, const FString& DatabasePath,
		FString& OutJson, FString& OutError);

	UECPANIMATIONEXT_API void HandleSetSchemaMirrorDataTable(const FString& SchemaPath, const FString& MirrorTablePath,
		FString& OutJson, FString& OutError);

	UECPANIMATIONEXT_API void HandleDuplicatePoseSearchChannel(const FString& SchemaPath, int32 ChannelIndex,
		FString& OutJson, FString& OutError);

	UECPANIMATIONEXT_API void HandleReorderPoseSearchChannels(const FString& SchemaPath, int32 FromIndex, int32 ToIndex,
		FString& OutJson, FString& OutError);

	UECPANIMATIONEXT_API void HandleListPoseSearchChannelTypes(FString& OutJson, FString& OutError);

	UECPANIMATIONEXT_API void HandleAddAnimationToDatabaseFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError);
	UECPANIMATIONEXT_API void HandleAddPoseSearchChannelFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError);
	UECPANIMATIONEXT_API void HandleCreatePoseSearchSchemaFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError);
	UECPANIMATIONEXT_API void HandleCreatePoseSearchDatabaseFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError);
	UECPANIMATIONEXT_API void HandleBuildPoseSearchDatabaseFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError);
	UECPANIMATIONEXT_API void HandleGetPoseSearchSummaryFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError);
	UECPANIMATIONEXT_API void HandleSetPoseSearchChannelPropertyFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError);
	UECPANIMATIONEXT_API void HandleSetPoseSearchSchemaSkeletonFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError);
	UECPANIMATIONEXT_API void HandleRemoveAnimationFromDatabaseFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError);
	UECPANIMATIONEXT_API void HandleSetPoseSearchSchemaPropertyFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError);
	UECPANIMATIONEXT_API void HandleRemovePoseSearchChannelFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError);
	UECPANIMATIONEXT_API void HandleSetAnimationDatabaseEntryPropertyFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError);
	UECPANIMATIONEXT_API void HandleSetPoseSearchDatabaseSchemaFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError);
	UECPANIMATIONEXT_API void HandleGetPoseSearchChannelPropertiesFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError);
	UECPANIMATIONEXT_API void HandleCreatePoseSearchNormalizationSetFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError);
	UECPANIMATIONEXT_API void HandleAddDatabaseToNormalizationSetFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError);
	UECPANIMATIONEXT_API void HandleSetSchemaMirrorDataTableFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError);
	UECPANIMATIONEXT_API void HandleDuplicatePoseSearchChannelFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError);
	UECPANIMATIONEXT_API void HandleReorderPoseSearchChannelsFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError);
	UECPANIMATIONEXT_API void HandleListPoseSearchChannelTypesFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError);
}
