# Axivor AI — Fase 2 (mundo), templates de animação (fase 3) e contexto/receitas (fase 4)

Data: 2026-09-07 (quinta rodada). Complementa `04_PLANO_PREMIUM_AUDITORIA.md` e `05_FASE1_IMPLEMENTADA_E_UE58.md`.

## 1. Umbrella `world` — `Source/Extensions/UECPWorldExt`

| Tool | Função | Parâmetros principais |
|---|---|---|
| `world_build_biome` | Camadas de vegetação por preset via PCG (`populate_landscape` por camada, com inclinação, espaçamento, escala e cull por preset) | `preset` (temperate_forest, pine_forest, jungle, meadow, desert, rocky, wetland), `meshes{trees[],bushes[],grass[],rocks[]}`, `landscape_label?`, `bounds_actor?`, `density_scale?`, `seed?` |
| `world_build_road` | Spline com pontos colados no chão + spline mesh por segmento, largura pelos bounds da mesh | `points[]` ou `spline_actor`, `mesh_path`, `width?=600`, `material_path?`, `closed?`, `align_to_ground?` |
| `world_build_river` | `WaterBodyRiver` do plugin Water (spline + largura/profundidade por reflexão) ou fallback em spline mesh | `points[]`, `width?=800`, `depth?=100`, `mesh_path?`, `use_water_plugin?` |
| `world_snap_to_grid` | Snap de posição (e opcionalmente rotação/escala) da seleção ou de labels | `actors[]?`/`selection`, `grid_size?=100`, `snap_rotation?`, `rotation_step?=15`, `snap_scale?` |
| `world_align_to_surface` | Trace até o chão por ator, alinhamento à normal opcional mantendo yaw, limite de inclinação | `actors[]?`/`selection`, `align_to_normal?`, `keep_yaw?`, `max_slope?`, `offset?` |
| `world_place_prefab` | Blueprints ou Level Instances em pontos, grade ou ao longo de spline, com yaw aleatório/fixo/olhando o caminho | `prefab`, `points[]`/`grid{}`/`along_spline{}`, `rotation_mode?`, `snap_to_ground?`, `scale_min/max?`, `max_count?` |
| `world_mass_edit` | Edição em massa por filtro (classe, label, tag, pasta, caixa, seleção): propriedades por reflexão, transformação, material, pasta, tags; `dry_run` | `filter{}`, `set{}`, `transform{}`, `material_path?`, `folder?`, `tags_add[]?`, `tags_remove[]?` |
| `world_landscape_import_heightmap` | Importa PNG/RAW/R16 com reamostragem (`FLandscapeImportHelper`) | `file_path`, `landscape_label?`, `scale_z?`, `edit_layer?`, `flip_y?` |
| `world_landscape_sculpt` | raise/lower/flatten/smooth numa região com falloff | `center`, `radius`, `mode`, `strength?`, `falloff?`, `target_height?` |
| `world_landscape_paint_layer` | Pintura de layer por regra de altura/inclinação, preenchimento ou círculo | `layer_name`, `mode=fill\|height_rule\|slope_rule\|circle`, limites, `strength?`, `invert?`, `erase?` |

Guia: `Source/Extensions/UECPWorldExt/Docs/world.md`. Todas com transação (undo).

## 2. Templates de animação — `UECPAnimationExt`

| Tool | Umbrella | O que faz |
|---|---|---|
| `create_locomotion_state_machine` | animation | Idle/WalkRun(blendspace ligado a Speed)/JumpStart/JumpLoop/JumpLand, regras (`Speed > 3`, `IsInAir`, automáticas), variáveis criadas, entry, ligação ao Output Pose, compile |
| `create_montage_from_sequence` | animation | Montage + slot + seções (tempo absoluto ou normalizado) + loops + blend + notifies (classe carregada) |
| `make_additive` | animation | AdditiveAnimType/RefPoseType/RefPoseSeq/RefFrameIndex consistentes + recompressão |
| `retarget_setup` | ik_retarget | IK Rigs origem/destino, roots e chains por heurística de nomes (Mixamo, UE, etc.), retargeter, auto-map, auto-align, export por animação |
| `setup_motion_matching` | pose_search | Schema (trajetória + ossos de pose), database, entradas, mirror table, índice, chooser |
| `setup_foot_ik` | animation | Local→Component → TwoBoneIK L/R → Component→Local antes do Output Pose, variáveis de alvo/alpha ligadas |

Também: `set_transition_rule` aceita `not_bool_variable`.

## 3. Contexto e receitas (fase 4)

- **Convenções do projeto** (`Source/UECPArchitect/Private/UECPProjectConventions.*`): bloco `=== PROJECT CONVENTIONS ===` injetado no prompt do Architect com engine/projeto, grade de snap do editor (posição/rotação/escala), GameMode/GameInstance/mapas padrão, pastas de topo com contagens, prefixos de nome dominantes, classes de asset mais comuns, plugins de gameplay ativos e existência do índice do scanner. Cache de 2 min.
- **GDD limitado** a 24k caracteres no prompt, com aviso de truncamento.
- **Umbrella `workflow`** (`Source/Extensions/UECPWorkflowExt`): `get_workflow(domain)` (sempre visível para a IA) devolve receitas ordenadas com ferramenta + verificação por passo para: gameplay_ability, open_world, locomotion_anim, montage, retarget, motion_matching, material, niagara, ui_widget, data_table, level_streaming, packaging, cpp_class, metahuman, pve_tree. `list_workflows` lista.

## 4. Pendências das fases (não feitas nesta rodada)
- Crew: status de checkpoint ainda é sintetizado; retries/oscilação/custo sem cap.
- Tarefas com campo `verify` mecânico (hoje a receita instrui a IA a verificar; não há gate automático por tarefa).
- Schema JSON tipado para as ações mais usadas e índice de docs sem perda (fase 1, item 5) continuam por fazer.
- Identidade de nós por `NodeGuid` (Blueprint) e handles de material por GUID.
