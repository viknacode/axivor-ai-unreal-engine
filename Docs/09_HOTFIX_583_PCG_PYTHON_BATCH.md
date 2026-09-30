# Axivor AI 1.7.0 — UE 5.8.3, PCG Python e Batch Processing

Data: 2026-09-30. Engine alvo: UE 5.8.3 (CL 58210709, instalado em `C:\Program Files\Epic Games\UE_5.8`).

## 1. Pesquisa

- **5.8.3** saiu em 22/09/2026 (5.8.1 em 28/07, 5.8.2 em 25/08). Não há 5.9 preview; UE6 Early Access previsto para o fim de 2027.
- Correções do 5.8.3 que afetam fluxos do Axivor: PCG (drift de atributos inteiros ao interpolar metadata, p. ex. Spline Sampler; Polygon Operation perdendo atributo do domínio Data), animação (state stack com 5+ entradas), Landscape (regressão do "Snap Under Brush Only" na foliage, crash ao fechar no modo paint), Sequencer (crash ao criar câmera sem viewport), MetaHuman (ML mask weights), Nanite (LOD com resolução dinâmica/split-screen).
- Nenhuma mudança em MCP, Toolset Registry, File Sandbox, PVE ou Control Rig nesse hotfix — as ferramentas existentes seguem válidas.
- APIs novas do 5.8 confirmadas no engine instalado e ainda não usadas pelo plugin:
  - `PCGPythonInterop` (beta, desligado por padrão): nó **PCG Python Data Processor** (`UPCGPythonDataProcessorSettings`) — Python com acesso direto aos dados PCG (`data_in` / `data_out`), pinos dinâmicos, script inline ou `.py`.
  - `UBatchProcessLibrary::RunBatch` (Kismet): distribui uma UFunction por N processos worker e devolve JSON com resultado por job.

Fontes: [5.8.3 Hotfix Released](https://forums.unrealengine.com/t/5-8-3-hotfix-released/2833315), [Simstim — Unreal 5.8.3](https://simstim.org/journal/unreal-news), [5.8 Release Notes](https://dev.epicgames.com/documentation/unreal-engine/unreal-engine-5-8-release-notes).

## 2. Implementado (`Source/Extensions/UECPUE58Ext`, umbrella `ue58`)

| Tool | Função |
|---|---|
| `ue58_feature_status` | Agora devolve `engine.version`/`changelist`/`branch` e `known_issues_fixed_in_later_hotfix[]` (vazio no 5.8.3). Também reporta `PCGPythonInterop`. |
| `pcg_python_processor` | Cria (via `add_pcg_node`) ou reconfigura (`node_index`) um nó Python Data Processor: `script` inline (valor padrão do pino `Source`) ou `script_file` (.py, relativo ao projeto), `inputs[]`/`outputs[]` (renomeia/adiciona/remove pinos dinâmicos), `mute_toast`. Transação + salvamento do grafo. |
| `blueprint_batch_process` | Chama `BatchProcessLibrary.RunBatch` por reflexão: `function` + `arguments[]`, ou `asset_class` + `path` (um job por asset, `max_jobs` para testar). Resume `passed/failed` e as 25 primeiras falhas. Bloqueia o editor até terminar. |

Classificação: as duas novas ferramentas são escrita. `UECPUE58Ext.Build.cs` passa a depender de `PCG` (já é dependência do plugin); a classe do processador continua resolvida por reflexão, então o módulo carrega com `PCGPythonInterop` desligado e responde com erro claro.

Guia da IA atualizado em `Source/Extensions/UECPUE58Ext/Docs/ue58.md` (contrato `data_in`/`data_out`, regra do `init_unreal.py` e do `if __name__ == '__main__'` para não disparar workers em cascata).

Versão do plugin: 1.6.0 → 1.7.0.

## 3. Como testar
1. Feche o editor e recompile o plugin (o `UECPUE58ExtModule.cpp` foi validado com compilação isolada; o link completo precisa do editor fechado por causa do Live Coding).
2. `ue58_feature_status` → `engine.version = 5.8.3`, lista de issues vazia.
3. `ue58_enable_plugins(plugins=['PCGPythonInterop'])` → reinicie.
4. Num grafo PCG: `pcg_python_processor(graph_path, script=..., outputs=['Output','Rejected'])` → ligue com `connect_pcg_nodes` → `generate_pcg`.
5. `blueprint_batch_process(function='/Game/Python/snapshot_assets_PY.SnapshotAsset:snapshot_asset', asset_class='Blueprint', max_jobs=5)`.

## 4. Limites
- `RunBatch` roda workers do editor; em projetos grandes demora e usa muita RAM — use `num_workers`.
- O Python Data Processor roda no game thread e não é cacheado.
- As notas de hotfix são uma lista fixa no código (`GHotfixNotes`); atualizar a cada novo 5.8.x.
