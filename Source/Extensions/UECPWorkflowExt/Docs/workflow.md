# `workflow` — build recipes with verification

Before any request that needs more than ~3 tool calls (a system, a level, an animation setup, a UI, a build), call `get_workflow(domain)` and turn the returned steps into tasks (`project_plan` / `task` tools). Each step has:

- `tool` — the umbrella/action(s) to use,
- `do` — what to achieve,
- `verify` — the read-back that proves it (compile result, summary tool, PIE observation, screenshot).

Rules:
1. Run the `verify` read-back before marking a step done. If it fails, fix it before moving on; never mark a step done on a tool that returned `success:false` or dropped items.
2. Keep the user's project conventions (see the PROJECT CONVENTIONS block in your context): naming prefixes, folders, grid snapping, default classes.
3. Prefer composite tools that already encode a recipe (`create_locomotion_state_machine`, `create_montage_from_sequence`, `retarget_setup`, `setup_motion_matching`, `world_build_biome`, `world_build_road`, `physics_asset_generate`, `gasp_add_character`) over long chains of primitives.
4. When a recipe mentions a plugin, confirm it with `ue58_feature_status` / Settings → Extensions before starting.

Domains: `gameplay_ability`, `open_world`, `locomotion_anim`, `montage`, `retarget`, `motion_matching`, `material`, `niagara`, `ui_widget`, `data_table`, `level_streaming`, `packaging`, `cpp_class`, `metahuman`, `pve_tree`. `list_workflows` shows them with step counts.
