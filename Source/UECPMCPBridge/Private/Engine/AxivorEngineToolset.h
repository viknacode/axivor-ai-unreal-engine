// Copyright 2026, BlueprintsLab, All rights reserved
//
// UE 5.9 — exposes the Axivor tool catalogue as a native toolset in the engine's
// AI Toolset Registry. Everything the registry serves (Epic's Unreal MCP server on
// :8000, the in-editor Unreal Assistant running Claude Code / Codex / Cursor, the
// MCP Client toolset, Python `toolset_registry`) can then reach every Axivor tool,
// routed through the same FRequestPipeline as Axivor's own MCP server — so the
// interaction modes (Just Chat / Plan / Auto Edit), crew permissions and destructive
// confirmations still apply.
//
// Toolset name: "Axivor"
//   Axivor.SearchTools  — find an action by English intent (wraps search_tools)
//   Axivor.GetToolDocs  — parameter docs for an umbrella / one action (wraps get_tool_docs)
//   Axivor.CallTool     — run any Axivor tool or umbrella action with a JSON arguments object
//
// Kept to three meta-tools on purpose: the catalogue has hundreds of actions and the
// engine's tool search already pages toolsets, so a lean surface costs far fewer tokens.

#pragma once

#include "CoreMinimal.h"
#include "ToolsetRegistry/Toolset.h"

namespace UECPEngineToolset
{
	// Registers the "Axivor" toolset in the engine registry (now, or as soon as the
	// ToolsetRegistry module loads). No-op when disabled by
	// [BpGeneratorUltimate] ExposeToEngineToolsetRegistry=False.
	void Register();

	// Unregisters the toolset and drops every delegate binding.
	void Unregister();

	// Whether the toolset is currently registered in the engine registry.
	bool IsRegistered();
}

class FAxivorEngineToolset final : public UE::ToolsetRegistry::FToolset
{
public:
	static const TCHAR* ToolsetName;

	virtual FString GetToolsetName() const override;
	virtual FString GetToolsetVersion() const override;
	virtual FString GetToolsetDescription() const override;

protected:
	virtual TFuture<TValueOrError<FString, FString>> ExecuteToolInternal(
		const FString& ToolName, const FString& JsonInput,
		UE::ToolsetRegistry::FToolCallContext Context) override;

	virtual UE::ToolsetRegistry::FToolsetJsonSchema GetJsonSchemaInternal() const override;
};
