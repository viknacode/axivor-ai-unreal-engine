---
name: axivor-editor-tools
description: Use the Axivor AI toolset to author and change Unreal Engine 5.9 content (Blueprint graphs, materials, Niagara, animation, Control Rig, PoseSearch, StateTree, Behavior Trees, PCG, GAS, UMG, levels) and to scan the project. Use when an agent connected through Unreal MCP or Unreal Assistant needs an editor action that the native toolsets do not cover, or covers only partly.
---

# Axivor AI editor tools

The Axivor plugin registers one native toolset, `Axivor`, in the engine's Toolset Registry. Unreal MCP (`list_toolsets` /
`describe_tools` / `call_tool`) and Unreal Assistant can call it like any engine toolset.

## Workflow

1. **Find the action.** Call `Axivor.SearchTools` with `query` set to English intent keywords (for example
   `"set material two sided"` or `"add IK to control rig"`). Use English even when the chat is in another language. The
   result lists matching actions with their umbrella and parameters, which is usually enough to call one.
2. **Read the parameters** only when the search result is not enough: call `Axivor.GetToolDocs` with
   `category=<umbrella>` and `action=<action>`. Leave out `action` to get the umbrella's recipes and known pitfalls.
3. **Run it.** Call `Axivor.CallTool` with `tool="<action>"` and `arguments={...}`. You can also pass an umbrella as
   `tool` and put the action in `arguments.action`.

Do not guess action names; always search first.

## Rules the toolset enforces

- **The user's Axivor interaction mode applies:**
  - **Just Chat:** read-only tools only.
  - **Plan:** read and plan tools only.
  - **Auto Edit:** full access. Destructive operations may still ask the user to confirm.
- A refused call returns an error that says why. Report it to the user; do not retry the same call.
- Results larger than 64 KB are truncated with `{"truncated":true}`. Call again with a narrower filter.

## Choosing between Axivor and the native toolsets

| Need | Prefer |
|---|---|
| Blueprint text DSL round-trip (read/write a whole graph) | `BlueprintToolsets` (Graph DSL) |
| Node-level Blueprint edits, K2 node wiring, Blueprint debugging | `Axivor.CallTool` (umbrella `blueprint`) |
| Console commands, cvars, cheats | `ConsoleToolset`, `CheatsToolset` |
| Profiling, stat captures | `PerfToolset`, `Axivor` umbrella `profiler` |
| Navmesh build/inspection | `NavMeshToolset` |
| Materials, Niagara, Control Rig, PoseSearch, Chooser, StateTree, PCG, GAS | `Axivor` (deeper coverage), native toolsets for simple property sets |
| Project-wide scan, GDD, crew agents, cost/perf reports | `Axivor` only |

You can also call native toolsets through Axivor: the `engine` umbrella (`engine_list_toolsets`,
`engine_describe_toolset`, `engine_call_tool`, `engine_sandbox`) runs them in-process. Wrap risky batches in the File
Sandbox: `engine_sandbox(action='enter')`, then `persist` or `discard`.
