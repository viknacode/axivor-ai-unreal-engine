# packaging umbrella

Package and cook the project through UnrealAutomationTool (UAT `BuildCookRun`), inspect
the setup, and follow a running build. This is the "ship loop" surface — it composes with
`git_tools` (commit/tag a release) and `play_test` (validate before packaging).

## Actions

- **get_packaging_settings** — Read `UProjectPackagingSettings`: build configuration, build
  target, pak/compression/debug flags, `MapsToCook`, always-cook directories, and the default
  game map. Call this first to know what a package would actually do.
- **list_target_platforms** — Enumerate the target platforms this editor install knows about
  (e.g. `Windows`, `WindowsServer`, `Linux`). Use the exact `name` values with `package_project`.
- **validate_packaging_setup** — Pre-flight check for common blockers: missing RunUAT, no build
  target, nothing to cook (no `MapsToCook` and no default map), or an unknown `platform`.
  Returns `issues[]` (severity `error`/`warning`/`info`) and `ready_to_package`. Run this before
  `package_project` and fix any `error` first.
- **package_project** — Launch `BuildCookRun` as a **background** job and return immediately with
  a `job_id`, `log_path` and the exact `command`. Params (all optional): `platform` (default
  `Win64`), `configuration` (`Debug`/`DebugGame`/`Development`/`Test`/`Shipping`; defaults to the
  project setting), `archive_directory` (default `<Project>/Saved/StagedBuilds`), `maps`
  (`+`-joined), `no_pak` (bool), `extra_args` (raw UAT flags appended verbatim). The build runs
  for several minutes — do not block on it.
- **get_packaging_status** — Poll a job by `job_id` (defaults to the most recent). Returns
  `state` (`running`/`succeeded`/`failed`), `exit_code` when finished, `elapsed_seconds`,
  `error_count`/`warning_count` scanned from the log, and a `log_tail` (`log_lines`, default 40,
  max 500). Poll periodically; when `finished` is true, read `exit_code` and the tail for the
  failure reason.

## Typical flow

1. `validate_packaging_setup` → fix any `error`.
2. `package_project { platform, configuration }` → keep the `job_id`.
3. `get_packaging_status { job_id }` every ~30s until `finished`.
4. On failure, read `log_tail` / open `log_path` to triage; on success, the staged build is under
   `archive_directory`.

## Notes

- Requires a valid engine installation with `Build/BatchFiles/RunUAT.bat` (Win) / `RunUAT.sh`.
- The job and its log live for the editor session; `log_path` persists on disk.
- Platform names come from `list_target_platforms`, not guesses — the SDK for a platform must be
  installed for its build to succeed.
