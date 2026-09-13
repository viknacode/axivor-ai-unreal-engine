// Copyright 2026, BlueprintsLab, All rights reserved

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

class UBlueprint;

namespace BlueprintCompareTools
{
	/**
	 * compare_blueprints: structural diff of two Blueprint assets.
	 * Args: blueprint_path_a (alias asset_path_a), blueprint_path_b (alias asset_path_b),
	 *       include_cdo? (default true), include_unchanged_graphs? (default false).
	 */
	UECPBLUEPRINTEXT_API void HandleCompareBlueprintsFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);

	/**
	 * snapshot_blueprint: duplicate a Blueprint into the transient package and keep it rooted
	 * so a later diff_blueprint_since_snapshot can report exactly what changed.
	 * Args: blueprint_path, label?
	 */
	UECPBLUEPRINTEXT_API void HandleSnapshotBlueprintFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);

	/**
	 * diff_blueprint_since_snapshot: diff the live asset against a snapshot taken earlier.
	 * Args: blueprint_path, snapshot_id? (default: latest snapshot for that path),
	 *       release_snapshot? (default false), include_cdo? (default true).
	 */
	UECPBLUEPRINTEXT_API void HandleDiffBlueprintSinceSnapshotFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);

	/**
	 * Diff engine shared by the three tools. A is "before", B is "after".
	 * LogicalIdsA / LogicalIdsB optionally override the per-node logical ids (keyed by NodeGuid)
	 * — used for snapshots whose package metadata is not duplicated.
	 * GuidRemapA maps a NodeGuid of A to the NodeGuid of the matching node in B, for duplicates
	 * whose node guids were regenerated.
	 */
	UECPBLUEPRINTEXT_API TSharedPtr<FJsonObject> DiffBlueprints(
		UBlueprint* A, UBlueprint* B,
		bool bIncludeCDO,
		bool bIncludeUnchangedGraphs,
		const TMap<FGuid, FString>* LogicalIdsA = nullptr,
		const TMap<FGuid, FString>* LogicalIdsB = nullptr,
		const TMap<FGuid, FGuid>* GuidRemapA = nullptr);

	/** Drop every rooted snapshot (module shutdown). */
	UECPBLUEPRINTEXT_API void ReleaseAllSnapshots();
}
