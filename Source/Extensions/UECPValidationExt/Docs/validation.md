# validation umbrella

Run the editor's **Data Validation** (the same validators as Editor → Validate Assets, plus any
project `UEditorValidatorBase` subclasses) over assets and get structured results. The pre-flight
companion to the `packaging` umbrella — catch broken references, missing data, and failing custom
validators *before* a cook, not after a 10-minute build fails.

## Actions

- **validate_asset** — Validate one asset. Param `asset_path` (a package path `/Game/Path/BP_Foo`
  or a full object path `/Game/Path/BP_Foo.BP_Foo`). Returns its `result` plus `errors[]` and
  `warnings[]`.
- **validate_assets** — Validate every asset under a folder. Params: `folder_path` (e.g.
  `/Game/Weapons`), `recursive` (default true), `max_reported` (default 100, max 1000). Returns the
  summary counts + per-asset `issues[]` (capped).
- **validate_project** — Validate all `/Game` assets. Param `max_reported` (default 100). Returns
  totals + the invalid/warning assets (capped). This loads every asset — expect it to take a while
  on a large project.

## Result shape

```
{
  "success": true,
  "num_checked": 812, "num_valid": 806, "num_invalid": 4, "num_warnings": 2,
  "num_skipped": 0, "num_unable_to_validate": 0,
  "total_problem_assets": 6,
  "truncated": false,
  "issues": [
    { "asset": "/Game/Weapons/BP_Rifle.BP_Rifle", "result": "invalid",
      "errors": ["Referenced material M_X is missing"], "warnings": [] }
  ]
}
```

## Notes

- Requires the **Data Validation** editor plugin (declared in this extension's `required_plugins`).
- Runs on the game thread and may load assets; validating the whole project is the slow path.
- Pairs with `packaging`: `validate_project` → fix `issues` → `package_project`.
