# Axivor AI — Auditoria e plano para um plugin premium

Data: 2026-09-07. Base: leitura do código-fonte do plugin (`Plugins/AxivorAI_UE5.8`) por quatro auditorias em paralelo: mundo aberto/cenário, animação, loop central da IA, e blueprint/material/niagara/crew. Todas as referências são `arquivo:linha` reais. Nada foi alterado nesta rodada.

## 0. Diagnóstico em uma frase

Os problemas que você viu não são "a IA sendo burra": são defeitos determinísticos nas ferramentas (ordem errada de rotação, nós de PCG mal configurados, AnimBlueprint nunca compilado) somados a um loop que não verifica o resultado do que fez. Consertar os 12 itens do nível 1 abaixo muda a experiência mais do que qualquer melhoria de prompt.

---

## 1. Por que o mundo aberto saiu torto (causas encontradas)

| # | Causa | Onde | Efeito |
|---|---|---|---|
| 1 | O scatter por PCG usa a **normal do terreno como eixo Z** dos pontos e o randomizador nunca liga `bAbsoluteRotation` | `PCGTools.cpp:872-874`, engine `PCGLandscapeCache.cpp:164` | Árvore inclina com a encosta e o yaw aleatório é aplicado num eixo torto |
| 2 | `set_pcg_transform_randomizer` escreve três propriedades **que não existem** (`bApplyToScale/Rotation/Position`) e reporta sucesso | `PCGTools.cpp:859,872,878` | Randomização "aplicada" não faz nada |
| 3 | `populate_landscape` cria o nó TransformPoints e **nunca o configura**; `min_scale`/`max_scale` são lidos e descartados; sem filtro de inclinação nem distância mínima | `LandscapeTools.cpp:820-825, 755-757` | Floresta sem variação, com sobreposição e em encostas íngremes |
| 4 | `spawn_advanced` sorteia **pitch e roll** além do yaw, Z aleatório dentro do bounding box **sem trace até o chão**, escala não uniforme | `LevelActorTools.cpp:703-723` | Árvores flutuando/enterradas, tombadas, esticadas |
| 5 | Arrays de rotação são `[Pitch, Yaw, Roll]`, o oposto do painel do editor (X=Roll, Y=Pitch, Z=Yaw), e isso não está documentado | `LevelActorTools.cpp:2817, 3972, 1587, 1121` | `[0,0,90]` para "girar 90°" vira **Roll 90** = árvore deitada; `[0,0,360]` = tombamento total |
| 6 | `paint_foliage` ignora todas as regras do FoliageType (AlignToNormal, GroundSlopeAngle, Radius, ZOffset, escala) e não tem distância mínima; cap de 500 instâncias | `FoliageTools.cpp:93-124` | Instâncias coladas, escala fixa, sem controle |
| 7 | `scatter_actors_along_spline` herda pitch/roll da spline e o modo "random" só troca o yaw; deslocamento sem re-trace | `SplineTools.cpp:384-396` | Objetos inclinados ao longo de rios/estradas |
| 8 | `set_spline_mesh` é um **stub** que devolve `success:true` | `SplineTools.cpp:336-343` | Estradas, rios e cercas por spline mesh são impossíveis |
| 9 | Zero `FScopedTransaction` no módulo de level design; spawn sempre cria `AStaticMeshActor` individual (sem ISM/HISM), sem World Partition/HLOD, busca por label é O(n·m) | vários | 2.000 árvores sem Ctrl+Z, lentas, sem streaming |
| 10 | Landscape: heightmap procedural bom, mas **sem sculpt, sem import de heightmap, sem pintura de layers** (weightmaps importados vazios) | `LandscapeTools.cpp:424, 700-706` | Terreno criado uma vez, sem material por camada |

**Correções prioritárias (ordem):** ligar `bAbsoluteRotation` e remover as propriedades fantasmas (1 linha cada); configurar o TransformPoints em `populate_landscape` com yaw 0–360, escala uniforme, filtro de inclinação e self-pruning; trace ao chão + yaw-only + escala uniforme em `spawn_advanced`; documentar/aceitar `{"yaw":90}` e rejeitar roll > 45° em scatter; honrar as regras do FoliageType; implementar `set_spline_mesh`; transação + índice de labels + opção `spawn_mode: ism|foliage`.

**Faltando para "premium":** presets de bioma (uma chamada monta a cadeia PCG correta), rede de estradas/rios por spline, snap à grade, alinhar à normal como opção, kit-bash/prefab, edição em massa por seleção, atribuição de grid de World Partition/HLOD no spawn.

---

## 2. Por que animação "sempre dá erro" (causas encontradas)

| # | Causa | Onde |
|---|---|---|
| 1 | **Nenhuma ferramenta de animação compila o AnimBlueprint** nem devolve erros do compilador; salva um asset quebrado e diz "sucesso" | `AnimationTools.cpp` (0 ocorrências de `CompileBlueprint`); modelo correto em `ControlRigTools.cpp:3049-3076` |
| 2 | `get_anim_state_machines` ("chame primeiro") procura em `FunctionGraphs` e **sempre devolve lista vazia**; `get_anim_graph_nodes` esconde os nós de state machine | `AnimationTools.cpp:6688-6691, 3409` |
| 3 | `build_anim_chain` troca Sequence **Player** por Sequence **Evaluator** (pose congelada) e reporta sucesso mesmo com todas as conexões falhando | `AnimationTools.cpp:6247-6256, 6314-6335` |
| 4 | Remover estado/transição deixa o BoundGraph órfão → recriar "Idle" vira "Idle_1" e tudo depois falha | `AnimationTools.cpp:3689-3692, 3745` |
| 5 | `add_state_alias` passa `nullptr` para `RenameGraphWithSuggestion` (assert) | `AnimationTools.cpp:10365-10367` |
| 6 | ~60 ferramentas usam `MarkBlueprintAsModified` em vez de `MarkBlueprintAsStructurallyModified`, e muitas não salvam | lista completa no relatório da auditoria |
| 7 | Blend space: eixos sem `PostEditChange` (grade/triangulação não recalculada), `bSnapToGrid`/`bWrapInput` inacessíveis, samples sem checagem de skeleton/limites | `AnimationTools.cpp:4670-4684, 2834-2848` |
| 8 | Notifies: classe resolvida com `FindObject` (não carrega) → degrada silenciosamente para notify por nome; sem `Link()` → tempos errados em montages | `AnimationTools.cpp:985-1014, 2969-2983` |
| 9 | Motion Matching: `build_pose_search_database` não indexa e o schema nunca é finalizado/salvo; Chooser nunca salva e não consegue vincular colunas (tabelas inertes) | `PoseSearchTools.cpp:342-350`, `ChooserTools.cpp` |
| 10 | `set_anim_node_property` não alcança nós dentro de state machines e não escreve enums (o caminho documentado para sync groups é um beco sem saída) | `AnimationTools.cpp:6769-6781, 6729-6757` |
| 11 | Descoberta de pino de pose usa "primeiro pino não-exec" (pode ser float/bool); resultado de `TryCreateConnection` ignorado | `AnimationTools.cpp:278-296, 360, 518` |
| 12 | Transição bidirecional sobrescreve a própria regra | `AnimationTools.cpp:624, 692-738` |

Mais 20 itens menores no relatório (frame rate inteiro forçado, curvas Vector viram Float, IK solver ignorando retorno, etc.).

**Faltando para "premium":** `compile_anim_blueprint`; template `create_locomotion_state_machine`; pipeline `retarget_setup` ponta a ponta; `setup_motion_matching`; criação de mirror table; `make_additive`; `create_montage_from_sequence` com seções; notifies em tempo normalizado; foot IK composto; templates de Control Rig; readback do grafo após cada mutação.

---

## 3. O loop da IA (por que ela não se corrige)

| # | Achado | Onde |
|---|---|---|
| 1 | **O sistema de verificação pós-turno existe e está desligado** (health check de blueprint, inspeção estrutural, nível não salvo): `ProcessArchitectResponse` tem zero chamadas; o único pós-check vivo é o auto-validate de Blueprints em Auto Edit | `SUECPMainWidget.cpp:1714`, `FUECPArchitectCoordinator.cpp:2786-2788, 244-397` |
| 2 | **Nenhuma ferramenta tem JSON schema**: tudo é `{action: string, ...qualquer coisa}`; parâmetros são strings livres sem tipo/enum/unidade | `FUECPAgentLoopBase.cpp:314-342`, `IUECPToolDispatcher.h:17-24` |
| 3 | Descoberta de ações só por `get_tool_docs`/`search_tools`, e ambos perdem informação (índice sobrescreve docs; 8 resultados por padrão; paginação anunciada e ignorada) | `FileSystemTools.cpp:424-455, 686-699, 744` |
| 4 | Sessões longas: histórico cresce sem limite dentro do loop, resultados de tool nunca truncados, `max_tokens` fixo 16k, só 1 retry em 429, **nenhum retry em 5xx**, requisições sem timeout | `FUECPClaudeAgentLoop.cpp:268, 497, 741-803` |
| 5 | Erros são strings; `hint` estruturado existe só em BlueprintGraphTools | `IUECPToolDispatcher.h:8-15` |
| 6 | Só 8 de 101 arquivos de tools usam transação; snapshots de diff não restauram nada e não persistem | `SUECPMainWidget.cpp:792-798, 821-960` |
| 7 | Plano = texto + checklist plano; sem receitas por domínio, sem checkpoints/rollback; tarefa N marcada "done" preenche as anteriores automaticamente | `PlanManager.cpp`, `TaskManager.cpp:94-113` |
| 8 | O scanner de projeto **não é injetado** no prompt do Architect; nenhuma convenção (grid, unidades, nomenclatura, pastas) é capturada; GDD sem limite de tamanho | `FUECPArchitectCoordinator.cpp:3259-3425` |
| 9 | Segurança: `export_*` classificado como leitura (inclui `export_text_to_file`), `run_command` (shell livre) é "Write" sem confirmação, `compile_blueprint` é "Read" mas apaga nós | `UECPToolSafety.cpp:71, 76`, `AssetManagementTools.cpp:885-921` |
| 10 | Zero testes automatizados; um único `dry_run` em todo o plugin | — |

---

## 4. Blueprint, material, dados, crew

- `build_blueprint_graph` **nunca compila** e não mapeia erros para os ids de nó que a IA passou; edições fora de transação; purga de nós órfãos sem undo (`BlueprintGraphTools.cpp:6545-13131, 9701-9752`).
- Hacks de SEH/validação de ponteiro escondendo um use-after-free real (`Args` mutado enquanto arrays apontam para ele) (`:119-214, 6046…`).
- `TryConnectWithSkelBypass` conecta pinos de classes diferentes (`BP_Enemy_1` ≈ `BP_Enemy_2`) reescrevendo o tipo (`:3806-3874`).
- Identidade de nó guardada no **comentário do nó** (`GEID:`), destruída se o usuário comenta (`:4797`). Deveria ser `NodeGuid` + sidecar.
- Resolução de pinos com 11 fallbacks sem checar tipo; `connect_pins` inverte direção sem avisar (`:575-838, 4952-4969`).
- Layout: recursão sem limite, tamanhos de nó chutados, sem resolução de sobreposição no caminho principal.
- Material: handles por índice de array (invalidam ao deletar), sem transação, validação lê erros de shader antes de compilar (`MaterialGraphTools.cpp:2537-2562`).
- DataTable: `const_cast` no row map, vazamento, campos desconhecidos descartados com `success:true` (`DataTableTools.cpp:193-280`).
- Input/StateTree/GAS: salvam em disco a cada edição sem transação; `success:true` mesmo com triggers/modifiers descartados.
- C++: arquivos salvos com auto-detect (vira UTF-16 com acento), sem backup/validação/compile após edição; `compile_project` bloqueia o editor e corta erros em 20 sem avisar.
- Crew: relatório de checkpoint é **sintetizado** do último texto do modelo ("success" = "o turno não deu erro de API"); sem contador de retries, sem detecção de ping-pong, sem orçamento de tokens; `bAutoApproveDestructive` true por padrão; gate de tools falha aberto.
- `wait_for_pie_event` e `run_pie_test_sequence` desabilitados no caminho do agente (bloqueiam thread).

---

## 5. Plano de execução (ordem recomendada)

### Fase 1 — Fundação (2 a 3 semanas). Muda a percepção de "bugado" para "confiável".
1. **Transação em todo handler** + `Modify()` (macro `UECP_TOOL_TRANSACTION`), turno inteiro como um undo. Adicionar `undo_last_tool_call`.
2. **Religar a verificação pós-turno** nos três agent loops (o código já existe) e estender a materiais, níveis, animação.
3. **Compilar após editar**: Blueprint (`build_blueprint_graph`), AnimBlueprint (todas), Material (aguardar shader), C++ (parse estruturado) — devolvendo erros ligados ao id do nó.
4. **`FUECPToolResult` com `code` + `hint`**; `success:false` sempre que qualquer sub-item foi descartado.
5. **Schema JSON tipado** para as 50 ações mais usadas (enum de `action`, tipos, unidades) e índice de docs sem perda.
6. Loop robusto: janela de histórico a cada rodada, truncar resultados (8–16k), retry 5xx com backoff, timeout, auto-compact durante o loop.
7. Segurança: `run_command` destrutivo com confirmação, `export_*` fora de "Read", `compile_blueprint` como Write, ACP usando `UECPToolSafety`.

### Fase 2 — Mundo aberto (1 a 2 semanas)
8. Os 7 fixes da seção 1 (rotação absoluta no PCG, `populate_landscape` completo, `spawn_advanced` com trace/yaw/escala uniforme, ordem de rotação, foliage por regras, `set_spline_mesh`, ISM/foliage no spawn).
9. Novo umbrella `world`: `build_biome(preset)`, `build_road(spline)`, `build_river(spline)`, `snap_to_grid`, `align_to_surface`, `place_prefab`, `mass_edit(selection)`.
10. Landscape: `import_heightmap`, `sculpt`, `paint_layer` com regras por altura/inclinação.

### Fase 3 — Animação (2 semanas)
11. Os 12 fixes da seção 2 (compile, descoberta de state machines, player vs evaluator, órfãos, structurally-modified, blend space, notifies com `Link`, pose pins).
12. Templates: `create_locomotion_state_machine`, `create_montage_from_sequence`, `retarget_setup`, `setup_motion_matching`, `make_additive`, foot IK.

### Fase 4 — Contexto e planejamento (1 a 2 semanas)
13. `project_profile` (grid, escala, nomenclatura, pastas, GameMode/Character) + digest do scanner injetados no prompt; GDD com limite.
14. Receitas por domínio (`get_workflow(domain)`), tarefas com `verify`, remover o preenchimento automático de tarefas anteriores.
15. Crew: status "unreported" em vez de "success" sintetizado, verificação obrigatória por checkpoint, retries/oscilação/custo com cap.

### Fase 5 — Qualidade contínua
16. `uecp_selftest` (registro × metadados × docs), `dry_run` genérico, testes de automação para a lógica pura, harness de smoke test por umbrella num projeto de exemplo.
17. Identidade de nó por `NodeGuid`, handles de material por GUID, diff real (nós/pinos/conexões) para `compare_blueprints`.

---

## 6. O que já foi feito nesta rodada
- Extensão `EngineToolsets` (umbrella `engine`) com todos os toolsets nativos do UE 5.8 + File Sandbox + servidor MCP nativo, **compilada com sucesso**.
- Modo Turbo (autonomia total) + sandbox como rede de segurança, com chips na barra de status.
- Detalhes em `03_TURBO_E_TOOLSETS_UE58.md`.

Relatórios completos das auditorias (com todos os trechos de código) estão nos transcritos dos agentes desta sessão; este documento consolida o essencial.
