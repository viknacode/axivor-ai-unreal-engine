// Copyright 2026, BlueprintsLab, All rights reserved.

#pragma once

#include "CoreMinimal.h"

class UBlueprint;
class UEdGraph;
class UEdGraphNode;

/**
 * Logical node identity sidecar.
 *
 * build_blueprint_graph / place_node give every node the AI creates a stable, human-readable
 * logical id ("PrintHello", "Branch_1", ...). Historically that id was stored in the node's
 * user-facing comment as "GEID:<id>", which leaked into the graph UI and was fragile (comments
 * are user-editable, copy/paste duplicated the tag, and every consumer re-parsed the string).
 *
 * The id now lives in the owning package's FMetaData (the engine's per-object editor metadata,
 * saved with the asset, moved on rename, never rendered in the graph). UBlueprint does not
 * implement IInterface_AssetUserData in 5.8 and an unreferenced sub-object would be garbage
 * collected, so package metadata is the engine-sanctioned sidecar.
 *
 * Storage: key UECP.LogicalId, value "<NodeGuid digits>|<logical id>". The NodeGuid prefix
 * guards against object-name reuse (metadata is keyed by object path); a value whose guid does
 * not match the node it is attached to is ignored.
 *
 * Legacy "GEID:" comments are migrated on read: the id is imported into the sidecar and the
 * token is stripped from the comment (other comment text is preserved).
 *
 * All helpers accept a null Blueprint — the package is derived from the node when needed.
 */
namespace BlueprintNodeIdentity
{
	/** Metadata key under which the logical id is stored. */
	UECPBLUEPRINTEXT_API const TCHAR* GetMetaDataKey();

	/** Assign (or replace) the logical id of a node. Also strips a legacy GEID comment token. */
	UECPBLUEPRINTEXT_API void SetLogicalId(UBlueprint* Blueprint, UEdGraphNode* Node, const FString& LogicalId);

	/** Returns the logical id, or empty when the node has none. Migrates a legacy GEID comment on the fly. */
	UECPBLUEPRINTEXT_API FString GetLogicalId(UBlueprint* Blueprint, const UEdGraphNode* Node);

	/** True when the node carries a logical id (sidecar or legacy comment). */
	UECPBLUEPRINTEXT_API bool HasLogicalId(UBlueprint* Blueprint, const UEdGraphNode* Node);

	/** Remove the logical id (call before deleting a node so no stale metadata is left behind). */
	UECPBLUEPRINTEXT_API void ClearLogicalId(UBlueprint* Blueprint, UEdGraphNode* Node);

	/**
	 * Find a node by logical id. PreferredGraph is searched first, then every graph of the
	 * Blueprint. Exact match wins over case-insensitive match.
	 */
	UECPBLUEPRINTEXT_API UEdGraphNode* FindNodeByLogicalId(UBlueprint* Blueprint, const FString& LogicalId, UEdGraph* PreferredGraph = nullptr);

	/** Find a node by logical id inside one graph only. */
	UECPBLUEPRINTEXT_API UEdGraphNode* FindNodeInGraphByLogicalId(UEdGraph* Graph, const FString& LogicalId, bool bCaseSensitive);

	/** The id surfaced to the AI: the logical id when present, otherwise the NodeGuid (32 hex digits). */
	UECPBLUEPRINTEXT_API FString GetNodeIdOrGuid(UBlueprint* Blueprint, const UEdGraphNode* Node);

	/** Walk every graph and migrate legacy GEID comments into the sidecar. Returns the number migrated. */
	UECPBLUEPRINTEXT_API int32 MigrateLegacyIds(UBlueprint* Blueprint);

	/**
	 * Strip a "GEID:<id>" token from a comment string. Returns true when a token was found;
	 * OutLegacyId receives the id. Remaining comment text (other lines) is preserved and trimmed.
	 */
	UECPBLUEPRINTEXT_API bool StripLegacyIdToken(FString& InOutComment, FString* OutLegacyId = nullptr);

	/** NodeGuid -> logical id for every node of every graph that has one. */
	UECPBLUEPRINTEXT_API TMap<FGuid, FString> CollectLogicalIds(UBlueprint* Blueprint);
}
