# `world` — World Builder (open-world composition)

High-level tools that chain the `landscape`, `spline`, `pcg` and `level_actor` umbrellas into whole workflows. Everything runs in one editor transaction per call (Ctrl+Z undoes a whole biome / road / mass edit). Nothing here needs an extra plugin: the **Water** plugin is optional and detected at runtime by `world_build_river`.

| Step | Tool | Composes |
|---|---|---|
| Terrain | `world_landscape_import_heightmap`, `world_landscape_sculpt`, `world_landscape_paint_layer`, `world_landscape_flatten_spline` | `FLandscapeEditDataInterface` (+ `create_landscape`, `add_landscape_layer_info`, `set_landscape_material` from `landscape`) |
| Vegetation | `world_build_biome` | `populate_landscape` (one PCG layer per zone/mesh group), `set_pcg_component_properties`, `set_pcg_exclusion`, `generate_pcg` |
| Roads / rivers | `world_build_road`, `world_build_river` | `spawn_spline_actor`, `set_spline_mesh`, `set_pcg_exclusion`, `world_landscape_flatten_spline`, or `WaterBodyRiver` by reflection |
| Set dressing | `world_place_prefab` | `SpawnActor` (Blueprint class) or `create_level_instance` |
| Bulk changes | `world_mass_edit` | reflection (`FProperty::ImportText_Direct`) |
| Clean-up | `world_snap_to_grid`, `world_align_to_surface` | direct transforms + line traces |

---

## Recommended open-world workflow

1. **Landscape** — `create_landscape(...)` (or an existing one), `set_landscape_material`, then either:
   - `world_landscape_import_heightmap(file_path, scale_z?)` for a DEM / World Machine / Gaea export (16-bit PNG or `.r16`), resampled automatically when the file size differs from the landscape;
   - or a few `world_landscape_sculpt` calls: `raise`/`lower` (strength in cm), `flatten` (to `target_height` or the centre height), `smooth`.
   Height mapping: uint16 `32768` = local Z 0; at `scale_z=100` the range is −256 m … +256 m. `center{x,y}` and `radius` are **world cm**.
2. **Material layers** — for each layer name in the landscape material (`LandscapeLayerBlend` / `LayerWeight` nodes): `add_landscape_layer_info(layer_name)` once (registers the layer's `LayerInfo` asset on the landscape's target-layer map — required before painting), then paint by rule with `world_landscape_paint_layer` (self-heals: if the layer isn't registered yet but the material declares it, it calls `add_landscape_layer_info` for you and retries instead of failing):
   - base: `mode=fill, layer_name=Grass`
   - cliffs: `mode=slope_rule, min_slope=35, layer_name=Rock` (`slope_blend=5` softens the edge)
   - beaches: `mode=height_rule, max_height=200, layer_name=Sand` (`height_blend=100`)
   - local spots: `mode=circle, center, radius, falloff`.
   Rules combine: any `min/max_height` or `min/max_slope` given also constrains `fill` and `circle`. Other layers are re-normalised by the engine.
3. **Biome** — `world_build_biome(preset, meshes{trees[],bushes[],grass[],rocks[]}, density_scale?, seed?, zone?{center,extent} | bounds_actor?, replace?=false, exclude?)`. Each provided group becomes a PCG actor `PCG_<Prefix>_<layer>_Actor` with graph `/Game/GeneratedPCG/PCG_<Prefix>_<layer>` (surface sampler → slope filter → self-pruning spacing → yaw-only transform → mesh spawner), `generation_trigger=GenerateOnDemand`, and a `set_pcg_exclusion` pass so it never scatters into buildings/roads. Groups without meshes are skipped and listed in `skipped[]` — tell the user which ones. Use `density_scale` (0.3–0.5 on km-scale landscapes) before touching individual graphs. **Give every biome call a disjoint `zone{center:{x,y}, extent:{x,y}}` (world cm half-extents) or `bounds_actor`** — calling `world_build_biome` again for the same `name_prefix` without `replace:true` fails and lists the existing `PCG_<prefix>_*` actors instead of silently stacking another full-landscape layer on top; pass `replace:true` to delete and rebuild that zone. See "Zones, budget and exclusions" below. Presets:

   | preset | trees /m² | bushes /m² | grass /m² | rocks /m² | notes |
   |---|---|---|---|---|---|
   | temperate_forest | 0.02 | 0.05 | 1.5 | 0.004 | broadleaf, dense understory |
   | pine_forest | 0.03 | 0.02 | 0.8 | 0.006 | tight conifers, sparse floor |
   | jungle | 0.05 | 0.15 | 2.5 | 0.003 | very heavy — lower density_scale |
   | meadow | 0.002 | 0.02 | 3.0 | 0.002 | open grassland |
   | desert | 0.001 | 0.01 | 0.2 | 0.01 | sparse, big rock scale range |
   | rocky | 0.003 | 0.01 | 0.4 | 0.03 | rocks allowed on steep slopes (80°) |
   | wetland | 0.008 | 0.06 | 2.0 | 0.002 | low slope limits keep the banks clean |

   Slope limits: trees 20–45°, grass ≤ 50°, rocks up to 80° (rocky). Tune later with `set_pcg_node_property` + `generate_pcg`, or `set_pcg_component_properties(seed)`.
4. **Roads** — `world_build_road(points[], mesh_path, width?=600, material_path?, clear_vegetation?=true, flatten_terrain?=false)`. Points are `[x,y,z]` **world cm**; Z is replaced by a ground trace (`align_to_ground=true`, `ground_offset=5` keeps the strip just above the terrain). The spline actor is a Blueprint in `/Game/World/Splines/<name>` spawned at the origin, tagged `AxivorRoad` + `Road`, tangents are auto-smoothed (`Curve`), one `SplineMeshComponent` per segment with **forward axis X**; the strip is scaled sideways so the mesh's native width (its bounds along Y) becomes `width`. Pass `spline_actor` instead of `points` to re-mesh an existing spline actor (for example after moving points with `set_spline_point_tangent`). `clear_vegetation` (default on) finds every PCG component whose generated bounds overlap the road and excludes a `width*1.3`-wide strip around the `AxivorRoad` spline tag, then regenerates it — reported per-actor in `vegetation_cleared[]`. `flatten_terrain` (default off) calls `world_landscape_flatten_spline(spline_actor, width)` afterwards so the road sits flush; result in `flatten_terrain`.
5. **Rivers** — `world_build_river(points[], width?=800, depth?=100)`. With the Water plugin enabled and `/Script/Water.WaterBodyRiver` loaded it spawns a real river (spline in world space, `RiverWidth` / `Depth` curves set on the water spline metadata, `PostEditMove` triggers the water mesh rebuild) and returns `path="water_plugin"`. Otherwise (`path="spline_mesh_fallback"`, reason included) it lays a spline-mesh strip from `mesh_path` slightly below the surface (`ground_offset=-20`). Terrain carving needs the Water Brush (landscape edit layer); tell the user if they expect a riverbed.
6. **Prefabs** — `world_place_prefab(prefab, points[] | grid{rows,cols,spacing,origin,yaw?,jitter?} | along_spline{actor,spacing,offset?,start_offset?}, rotation_mode, snap_to_ground)`. `prefab` is a Blueprint asset path (spawned as its generated class), a `/Script/...` class, or a **level asset** (spawned as Level Instances through `create_level_instance`). Labels are `<name_prefix>_001…` — reuse the prefix in `world_mass_edit(filter.label_prefix)`.
7. **Mass edit** — `world_mass_edit(filter, set, transform, material_path, folder, tags_add)`. Always give at least one filter criterion (the tool refuses an empty filter). Run with `dry_run=true` first when the match count is uncertain. `set` keys: `"Mobility": "Static"`, `"StaticMeshComponent.CastShadow": false`, `"LightComponent.Intensity": 5000`, struct values as objects (`{"X":1,"Y":2,"Z":3}` → `(X=1,Y=2,Z=3)`), enums by name, assets by path. `transform.offset_rotation` is added per axis (yaw-only offsets are exact).
8. **Snap / align** — `world_snap_to_grid` for modular kits (`grid_size`, `snap_rotation` + `rotation_step`), `world_align_to_surface` for props on terrain (`align_to_normal=true, max_slope=45` for rocks/logs; keep `align_to_normal=false` for buildings). Both default to the **editor selection** when `actors[]` is omitted; every skipped actor is reported with a reason.

## Zones, budget and exclusions

- **One PCG actor per zone, never the whole map per biome.** `world_build_biome` refuses to run again for the same `name_prefix` while `PCG_<prefix>_*_Actor` actors already exist, unless `replace:true` (which deletes and rebuilds them inside the same transaction). When building several biomes in one level, give each a **disjoint** `zone{center:{x,y}, extent:{x,y}}` (world cm half-extents; Z extent is taken from the landscape or defaults to 5000) or a `bounds_actor` — two biomes sharing overlapping/whole-map coverage double the instance count and fight each other's exclusions. Omitting both covers the entire landscape, which is fine for a single biome pass but not for layering multiple biomes.
- **Check the budget before adding more vegetation.** Call `pcg.get_pcg_level_summary` and keep `total_instances` under the target platform's budget; lower `density_scale` or shrink the `zone` instead of scattering everywhere at full density.
- **Exclusions keep vegetation out of buildings and roads.** `world_build_biome`'s `exclude?{world_collision?=true, actor_tags?[], actor_classes?[], spline_tags?=["AxivorRoad"], spline_width?=600, margin?=0.15}` is applied to every layer via `set_pcg_exclusion` after the graph is built and before the final `generate_pcg`; a failure is recorded per-layer (`layers[].exclusion.error`) but does not fail the whole biome. `world_build_road(clear_vegetation:true)` does the inverse after the fact: it finds every already-generated PCG component overlapping the new road and excludes/regenerates it. Both rely on roads being tagged `AxivorRoad` (`world_build_road` does this automatically; spline actors built another way should get the same tag if they should block vegetation).
- **Generation trigger.** `world_build_biome` sets `generation_trigger=GenerateOnDemand` on every layer's PCG component so vegetation isn't silently rebuilt on every load / PIE session — call `generate_pcg(actor_label, force=true)` explicitly after further tuning.

## Rotation & coordinate conventions

- Units are **centimetres**; Z is up; yaw rotates around Z, positive = counter-clockwise seen from above (UE left-handed convention: +X forward, +Y right).
- Rotations in arguments are `[pitch, yaw, roll]` arrays or `{pitch, yaw, roll}` objects in **degrees** (same as `spawn_actor`). Results report `{pitch, yaw, roll}`.
- `rotation_mode=face_path` yaws the prefab along the path direction (spline tangent, or towards the next point); add `rotation.yaw` to offset it (e.g. `90` when the mesh faces +Y).
- `align_to_normal=true` builds the rotation with `FRotationMatrix::MakeFromZX(normal, forward-of-current-yaw)`: Z follows the surface, the actor keeps facing its yaw. `keep_yaw=false` uses `MakeFromZ` (any yaw).
- Spline meshes use **forward axis X**: author road/river meshes along +X with the width along Y; `width` scales Y (and leaves Z).
- Landscape vertex coordinates (`region` in results) are quads in landscape-local space; world XY → vertex uses the landscape transform, so rotated / scaled landscapes work. Slopes are in degrees from horizontal.
- `grid.yaw` rotates the whole grid; rows advance along the grid's local +Y (left of `yaw` direction), columns along +X.

## Failure modes to report to the user

- `populate_landscape` / `spawn_spline_actor` not registered → Level Design extension disabled.
- Layer info missing and the material doesn't declare that layer name → `world_landscape_paint_layer` fails listing both the known landscape layers and the material's layer names; otherwise it self-heals via `add_landscape_layer_info` automatically.
- `world_build_biome` fails with existing `PCG_<prefix>_*` actors listed → pass `replace:true` to rebuild that zone, or use a different `name_prefix` / `zone`.
- Landscape with **edit layers**: edits go to the current editing layer (or `edit_layer=<name>`); procedural layers are refused. Without layers, edits are applied directly and collision is rebuilt. Applies to `world_landscape_sculpt`, `world_landscape_paint_layer` and `world_landscape_flatten_spline`.
- Heightmap import on World Partition landscapes with unloaded proxies edits only the loaded extent — load the region first.
- Water path returns `width_applied=false` when the plugin's metadata layout changed; the river still exists and can be adjusted in the Details panel.
- `set_pcg_exclusion` not registered (pcg extension not yet updated) → biome/road exclusion steps report the error per layer/actor but the biome/road itself still succeeds; rerun `world_build_biome`/`world_build_road` once the tool is available, or call `set_pcg_exclusion` manually.
