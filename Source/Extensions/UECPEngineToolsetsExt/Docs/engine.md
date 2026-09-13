# `engine` — Unreal Engine 5.8 native toolsets (in-process)

Unreal Engine 5.8 ships a **Toolset Registry**: every editor feature that Epic exposed to AI agents (the same layer served by the built-in *Unreal MCP* HTTP server) is registered as a *toolset* made of *tools* with JSON schemas. This umbrella calls that registry directly inside the editor process, so no external server is required and the calls follow the current interaction mode (Just Chat = read-only, Ask Before Edit, Auto Edit, Plan).

Typical toolsets (the exact list depends on which engine plugins are enabled; always discover at runtime):
`SceneTools`, `ActorTools`, `MaterialInstanceTools`, `ObjectTools`, `EditorApp`, `Logs`, `UMG`, `PCG`, `Niagara`, `GAS` (abilities, attributes, cues), `PhysicsAsset`, `SlateInspector`, `AutomationTest`, `SemanticSearch`, `Plugin`, `ConfigSettings`, `GameFeatures`, `GameplayTags`, `DataRegistry`, `MVVM`, `StateTree`, `LiveCoding`, `AgentSkill`, `DataflowAgent`, `ChaosClothAsset`, `WorldConditions`, `MCPClient`.

## Workflow

1. `engine_list_toolsets(filter?)` — discover toolsets and their tool names. Use `filter` (e.g. `"actor"`, `"material"`) to keep the answer small.
2. `engine_describe_toolset(toolset)` — get the JSON schema for the toolset's tools (parameter names, types, descriptions).
3. `engine_call_tool(toolset, tool, input)` — run it. `input` is a JSON **object** matching the schema. You may also pass `name="Toolset.Tool"`.
4. If the answer has `status: "pending"`, poll `engine_call_tool_result(job_id)` until `status: "done"`.

Prefer Axivor's specialised umbrellas (`blueprint`, `material`, `level_actor`, `niagara`, ...) when they cover the task — they carry richer validation. Reach for `engine` when you need something they do not cover (Slate inspection, automation tests, plugin management, config settings, semantic search, agent skills, PCG/Dataflow authoring, UMG at the widget-tree level, etc.).

## File Sandbox (safe experimentation)

`engine_sandbox(action)` wraps UE 5.8's File Sandbox: while a sandbox is active, asset and file writes are captured instead of hitting the project directly.

- `status` / `changes` — is a sandbox active, and which files changed (`added` / `edited` / `removed`).
- `enter(name?, description?)` — start (or resume) a sandbox. Default name `AxivorAI`.
- `persist(files?)` — write the captured changes to the real project (all changed files when `files` is omitted).
- `discard(files?)` — throw the captured changes away (all, or only `files`).
- `leave` — exit the sandbox without deleting it.

When **Turbo mode + sandbox** is on in Settings, Axivor enters the `AxivorAI` sandbox automatically at startup; the status bar shows the pending change count and the user can apply or discard from there. Before persisting, summarise the changed files for the user.

## Native MCP server (external agents)

`engine_mcp_server(action)` controls Epic's built-in MCP HTTP server (plugin *Unreal MCP*, `ModelContextProtocol`), which lets external agents such as Claude Code, Cursor, VS Code, Gemini CLI or Codex drive this editor:

- `status` — whether the plugin is loaded.
- `start(port?=8000)` — start the server at `http://127.0.0.1:<port>/mcp` (loopback only, no auth by design).
- `generate_client_config(client=ClaudeCode|Cursor|VSCode|Gemini|Codex|All)` — write `.mcp.json` in the project root.

## Gotchas

- Toolsets are **experimental** in 5.8: schemas can change between engine versions. Always describe before calling.
- Tool names are case-sensitive and use the `Toolset.Tool` form.
- Blocked toolsets/tools (Editor Preferences → Plugins → Toolset Registry → Blocked/Allowed names) are not listed and cannot be called.
- `SetObjectProperties`-style tools honour the registry's blocked classes/properties lists.
