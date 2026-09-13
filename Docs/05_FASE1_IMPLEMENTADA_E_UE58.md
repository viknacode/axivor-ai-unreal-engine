# Axivor AI — Fase 1 implementada + pacote de recursos UE 5.8

Data: 2026-09-07 (quarta rodada). Complementa `04_PLANO_PREMIUM_AUDITORIA.md`.

## 1. Fase 1 do plano (implementada e compilada)

### Mundo aberto (`Source/Extensions/UECPLevelDesignExt`)
- `set_pcg_transform_randomizer`: `bAbsoluteRotation` ligado por padrão (`absolute_rotation`), propriedades fantasmas removidas, setters reportam propriedades ausentes em `warnings[]`, novos `rot_min_x/rot_max_x` (pitch) e `rot_min_y/rot_max_y` (roll).
- `populate_landscape`: TransformPoints configurado (rotação absoluta, yaw 0–360, escala uniforme de `min_scale/max_scale`), filtro de inclinação (`max_slope_degrees`, padrão 35) via NormalToDensity+DensityFilter, distância mínima (`min_spacing`) via SelfPruning, transação.
- `spawn_advanced`: trace até o chão (`align_to_ground`), alinhamento à normal opcional (`align_to_normal`), yaw-only por padrão (`allow_tilt` para permitir pitch/roll), escala uniforme (`uniform_scale`), `spawn_mode: actors|ism` (HISM), contadores `spawned/skipped_no_ground`.
- Rotação: arrays continuam `[pitch,yaw,roll]`, objetos `{yaw,pitch,roll}` e `{x:roll,y:pitch,z:yaw}` aceitos; guarda contra roll > 45° sem `allow_tilt`; metadados de todas as ferramentas explicam a ordem.
- `paint_foliage`: respeita o FoliageType (Radius/distância mínima, GroundSlopeAngle, AlignToNormal+AlignMaxAngle, RandomYaw, RandomPitchAngle, ZOffset, escala), `max_instances`, contadores `placed/skipped_*`.
- `set_spline_mesh` implementado de verdade (um SplineMeshComponent por segmento, tag `AxivorSplineMesh`); `scatter_actors_along_spline` com `rotation_mode` (`align_to_spline|yaw_only|random|none|fixed`), re-trace ao chão após offset.
- Transações (`FScopedTransaction`) e índice de labels nos handlers em lote.

### Animação (`Source/Extensions/UECPAnimationExt`)
- Compilação com relatório em todos os handlers mutantes (`compiled`, `num_errors`, `compile_errors[]` com título/GUID do nó); nova ação `compile_anim_blueprint`.
- `get_anim_state_machines` e `get_anim_graph_nodes` enxergam state machines e pinos livres.
- `build_anim_chain`: Sequence Player correto, `blendspace_player`, `connections[]` com status por aresta, sucesso só se tudo conectou.
- Remoção de estados/transições sem grafos órfãos; `add_state_alias` sem crash; varredura `MarkBlueprintAsStructurallyModified` + salvar; pinos de pose resolvidos por `IsPosePin`; `FindAnimGraph` prefere `AnimGraph`; erro claro quando há mais de uma state machine sem nome.
- Transições bidirecionais viram dois nós independentes; `set_transition_rule` notifica o grafo.
- `set_anim_node_property` alcança nós dentro de state machines e escreve enums/objetos/structs.
- Blend space: `snap_to_grid`/`wrap_input`, validação de eixos e samples, `PostEditChange`.
- Notifies: classe carregada com erro se não existir, `Link()`/`SetDuration`, `time_normalized`.
- Chooser salva sempre; Pose Search finaliza schema e indexa o database (`RequestAsyncBuildIndex`).

### Loop da IA (`UECPArchitect`, `UECPShell`, `UECPCore`, `UECPMCPBridge`)
- Gate de verificação pós-turno religado nos três loops (Claude, OpenAI/DeepSeek/Custom, Gemini): health check de Blueprint, inspeção estrutural e nível não salvo geram uma continuação sintética (Auto Edit / Ask). `ProcessArchitectResponse` (705 linhas mortas) removido.
- Retry 429/5xx/529 e falhas de transporte (3 tentativas, backoff 1/3/8 s), timeout por requisição (`RequestTimeoutSeconds`), `MaxOutputTokens` configurável.
- Janela de contexto por rodada (320k chars, elide resultados antigos mantendo ids), truncamento de resultado de ferramenta (16k) com marcador.
- Segurança: `export_*` deixou de ser leitura; `run_command`, `git_revert`, `git_checkout` destrutivos; `compile_blueprint` é escrita.
- TaskManager sem preenchimento automático; bridge MCP com limite de 64 KB por resultado.
- Extras: C++ gerado em UTF-8; erros de `compile_blueprint` com nó/GUID/id; mapeamento de input parcial vira erro; importação com diagnóstico.

## 2. Pacote UE 5.8 (`Source/Extensions/UECPUE58Ext`, umbrella `ue58`)

| Tool | Função |
|---|---|
| `ue58_feature_status` | Plugins 5.8 ativos + flags de renderização |
| `ue58_enable_plugins(plugins[])` | Ativa plugins no .uproject (reinício) |
| `pve_create_vegetation` / `pve_list_nodes` / `pve_open_editor` | Procedural Vegetation Editor: cria o asset (grafo PCG), lista os nós `PV*Settings` com propriedades, abre o editor. Autoria via umbrella `pcg` |
| `controlrig_physics_from_physics_asset` | Control Rig Physics: solver + Instantiate From Physics Asset no evento Construction |
| `dmc_add_component` | Direct Mesh Controls: componente DMC num Blueprint |
| `metahuman_api` | MetaHuman Character (Python): probe, can_build/build, export_dna, conform_body_from_mesh (Mesh to MetaHuman), python livre |
| `render_apply_preset` / `render_set_cvars` / `render_get_state` | MegaLights, Lumen Lite, Substrate/NPR, FSSS, Nanite Foliage, HWRT Lumen (cvars + project settings) |
| `physics_asset_generate` | Physics Asset a partir do skeletal mesh em uma chamada (iteração rápida) |
| `gasp_guide` / `gasp_add_character` | Game Animation Sample 5.8: guia por tópico e criação do personagem filho (retarget mesh, ABP, tag, `IKRetargeter_Map`) |

Guia para a IA: `Source/Extensions/UECPUE58Ext/Docs/ue58.md`. O `add_pcg_node` já aceita qualquer subclasse de `UPCGSettings` carregada (inclusive PVE e PCGWaterInterop).

### Pesquisa (fontes)
- Release notes 5.8: PCG (Align Points, arrays de metadata, Apply Spline to Component, Teleport, HLSL, GPU scatter), PVE (growth nodes, extração de skeleton de mesh/atlas, grafting, carving, Nanite foliage), Control Rig Physics beta (rigs em camadas, forças animáveis, módulos), Control Rig Dynamics (partículas, 5x), DMC experimental, Skeletal Mesh editor (blendshapes, joint locking, redução de bones), MetaHuman (Mesh to MetaHuman com head+body num passo, Creator com texturas não-baked e Lumen, Crowd experimental, Animator body capture), MegaLights production-ready, Lumen Lite beta, Substrate NPR/AxF, FSSS, Unreal MCP.
- Game Animation Sample 5.8: Pose Search Interaction Assets, `SandboxCharacter_Mover_Ragdoll` com Physics Control, Chooser com Pose Match column + blend spaces, Look-At POI, Smart Objects/State Tree.

Fontes: [Release notes](https://dev.epicgames.com/documentation/unreal-engine/unreal-engine-5-8-release-notes), [GASP doc](https://dev.epicgames.com/documentation/en-us/unreal-engine/game-animation-sample-project-in-unreal-engine), [GASP 5.8 (Rookies)](https://www.therookies.co/blog/headlines/unreal-engines-game-animation-sample-project), [Unreal MCP](https://dev.epicgames.com/documentation/unreal-engine/unreal-mcp-in-unreal-editor).

## 3. Como testar
1. Reinicie o editor (DLLs novos). Em Settings → Extensions confirme "Unreal 5.8 Feature Pack" e "Unreal 5.8 Native Toolsets" carregados.
2. No chat: `ue58_feature_status` → ative o que faltar com `ue58_enable_plugins` → reinicie.
3. Mundo aberto: "crie uma floresta na landscape" → a IA deve usar `populate_landscape` (yaw-only, sem inclinação, sem sobreposição) ou `spawn_advanced(align_to_ground)`.
4. Animação: "crie um state machine Idle/Walk/Run" → resposta traz `compile_errors[]` vazio e `connections[]` todos `connected:true`.
