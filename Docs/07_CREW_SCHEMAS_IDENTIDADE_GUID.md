# Axivor AI — Crew com evidência, schemas tipados, identidade por GUID e tarefas verificadas

Data: 2026-09-08 (sexta rodada). Fecha as pendências listadas em `06_FASE2_MUNDO_TEMPLATES_RECEITAS.md` §4.

## 1. Crew: checkpoints com evidência e caps de custo

Antes, o status de cada checkpoint era sintetizado pelo roteador (um agente que "dizia" que terminou passava). Agora:

- **Relato explícito**: o handoff só conta como concluído quando o agente chama `checkpoint_pass` com sucesso explícito **e** evidência de leitura (tool calls read-only executadas no checkpoint). Sem relato, o status é `unreported` e o checkpoint não avança (fail-closed).
- **Grounding**: `FUECPCrewCoordinator` coleta as tool calls do checkpoint; `bGrounded` só é verdadeiro se houve verificação real (não apenas texto). `ReportGrounding` acumula o histórico do run.
- **Retries e oscilação**: `MaxRetriesPerCheckpoint` (padrão 3). Um checkpoint que alterna entre pass/fail repetidamente é encerrado com erro em vez de girar para sempre.
- **Tokens**: `MaxTotalTokens` (0 = sem limite) e `TotalTokensUsed` no run; ao estourar, o run pausa com motivo.
- **Destrutivo**: `bAutoApproveDestructive` padrão `false` — ferramentas Destructive no Crew pedem aprovação mesmo em Turbo.
- **UI/bridge**: `CrewRunDetailJson` expõe `maxRetriesPerCheckpoint`, `maxTotalTokens`, `totalTokensUsed`, `reportGrounding`, por checkpoint `retryCount/lastReportStatus/verificationText/grounded/verificationToolCalls`, por handoff `explicitReport/verification/verificationToolCalls`. O save de caps aceita os dois novos campos.

Arquivos: `Source/UECPCore/Public/Crew/CrewTypes.h`, `Source/UECPCore/Private/Crew/*` (Coordinator, HandoffRouter, RoleContextAssembler, CrewTools), `Source/UECPShell/Private/Widget/UUECPAppBridge.cpp`.

## 2. Tarefas com `verify` mecânico

- `FTask` ganhou `Verify` (o que provar) e `Verification` (o que foi observado).
- `task(action='update_task', index, status='done')` numa tarefa com `verify` **é recusado** sem `verification='...'`. O erro devolve a instrução de read-back para a IA executar antes.
- `set_tasks` aceita `items:[{content, status?, verify?}]`; `add_task` aceita `verify?`; batch em `update_task` aceita `verification?` por item.
- `GetTasksForAI` imprime `verify:` / `verified:` por tarefa; o workflow (`get_workflow`) já entrega o `verify` por passo, então a IA pode copiá-lo direto para a tarefa.

Arquivos: `AssetReferenceTypes.h`, `Managers/TaskManager.h/.cpp`, `SUECPMainWidget_ToolExecution_Task.cpp`, metadata em `UECPToolsModule.cpp`.

## 3. Schemas JSON tipados por umbrella

- `FUECPToolMeta.Params` (texto tipo `actor_label, location?, rotation? {yaw,pitch,roll}`) agora é **parseado** (`UECPToolDispatch::ParseParamSpec`) em `FUECPParsedParam` com tipo inferido: number/boolean/string/array/object, enums de `a|b`, vetores `{x,y,z}`, rotators `{pitch,yaw,roll}`, arrays de objetos `[{...}]` até 3 níveis, `=default`.
- `IUECPToolDispatcher::GetUmbrellaSchema(FName)` devolve um schema mesclado por umbrella (cache invalidado ao registrar/remover metadata): enum de `action`, cada parâmetro com `used by: a, b (+N)` e o token original na descrição. Cap de 120 propriedades.
- Consumidores: loops Claude/OpenAI (agora emitem `input_schema` para umbrellas), Gemini (`SanitizeSchemaForGemini`, remove `additionalProperties`, omite objetos sem sub-campos), catálogo MCP (`McpToolsCatalog`), registro de extensões.
- Resultado prático: os provedores validam tipos antes de chamar; erros clássicos como `rotation` como número ou `actors` como string desaparecem na origem.

## 4. Docs de ferramentas sem perda

`get_tool_docs`:
- `action=` devolve `summary`/`params` do registro **e** o bullet do doc (`source: registry+doc|registry|doc`).
- `max_chars` (padrão 12000, `0` = ilimitado) e `offset`, com `total_chars`, `truncated`, `next_offset`; corte em fim de linha.
- Índice mescla o registro com os docs (`summary` quando difere).
- `search_tools` padrão 15 resultados e `total_matches`.

## 5. `uecp_selftest` (drift do registro)

`uecp_selftest(scope?=all|<umbrella>, include_docs?=true)` (read-only) relata: handlers sem metadata, metadata sem handler, docs de ferramentas não registradas (separando extensões não carregadas), umbrellas sem doc, `params_unparseable` (tokens tipo `<properties>`), e diferenças entre `owned_tools` do manifesto e o que a extensão realmente registrou. Use após adicionar/renomear ferramentas. Hoje sobram 32 specs com placeholders — cosmético, mas aparece no relatório.

## 6. Identidade de nós Blueprint por GUID

- O id lógico (`GEID`) saiu do **comentário do nó** e foi para metadata do pacote (`FMetaData`, chave `UECP.LogicalId`, valor `"<NodeGuid>|<id>"`). Salva com o asset, acompanha rename, não aparece no gráfico. Comentários `GEID:` antigos são migrados ao carregar o Blueprint (`BlueprintNodeIdentity::MigrateLegacyIds`), preservando o texto restante.
- Helpers em `Extensions/UECPBlueprintExt/Public/Tools/BlueprintNodeIdentity.h`: `Set/Get/Has/ClearLogicalId`, `FindNodeByLogicalId`, `GetNodeIdOrGuid`, `CollectLogicalIds`.
- Todos os leitores foram atualizados: BlueprintGraphTools, BlueprintDeletionTools, `compile_blueprint` (refs de nó), describer (`BpGraphDescriber`) e a detecção de `geid_only` no arrange do shell.

## 7. `compare_blueprints` real + snapshots

- Diff estruturado: variáveis, componentes, interfaces, event dispatchers, funções, macros, gráficos (nós adicionados/removidos/alterados por NodeGuid → id lógico, defaults de pin, links), CDO por propriedade e `summary`.
- `snapshot_blueprint(blueprint_path, label?)` → `snapshot_id` (duplicata transiente, máx. 3 por asset). `diff_blueprint_since_snapshot(blueprint_path, snapshot_id?, release_snapshot?, include_cdo?)` mostra o que a IA mudou desde o snapshot — a receita recomendada é snapshot → editar → diff → compile.

## 8. Material graph com handles por GUID

- Todo nó de material é endereçado por `node_id` (GUID da expressão), com aceitação de índice/`n3`/`node[3]` por compatibilidade. Todas as respostas ecoam `node_id`; `get_material_nodes` traz `connections_by_id`, export/import mapeiam `node_id_map`.
- Toda mutação está em `FScopedTransaction` (undo funciona).
- `validate_material` espera a compilação de shader (`compile_timeout_seconds`, padrão 60) e devolve `compilation_pending` em vez de um falso `is_valid`.

## 9. Como verificar

1. `uecp_selftest` → `ok:true` (ou apenas `params_unparseable`).
2. `get_tool_docs(category='level_actor', action='spawn_advanced')` → `source: registry+doc`.
3. Blueprint: `snapshot_blueprint` → `place_node` → `diff_blueprint_since_snapshot` lista o nó em `added_nodes` com `id`.
4. Tarefa com `verify`: `update_task(status='done')` sem `verification` deve ser recusado.
5. Crew: rodar um run com um checkpoint que não chama `checkpoint_pass` → status `unreported`, não avança.

## 10. Comportamentos alterados
- `compare_blueprints.variables` não lista mais event dispatchers (agora em `event_dispatchers`).
- `get_tool_docs` de categoria vem limitado a 12k chars por padrão (paginável).
- Ids de nó novos não aparecem mais como comentário no gráfico.
