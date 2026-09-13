# Axivor AI — PCG com zonas, exclusão e orçamento; confirmação que decide sozinha

Data: 2026-09-08 (sétima rodada). Corrige o caso real do mapa SkyTown: a segunda passada ("faltou biomas, terreno de grama, estradas") empilhou 4 florestas sobre o mesmo terreno, colocou árvores dentro das casas e travou o editor.

## 1. O que deu errado (diagnóstico pelo log da sessão)

Lendo `Saved/Logs/*.log` e o histórico do chat, a segunda rodada fez exatamente isto:

1. `duplicate_pcg_graph` × 3 (PG_SkyTown_Forest → AutumnForest, PineForest, Meadow).
2. `spawn_pcg_actor` × 3 + `set_pcg_actor_bounds` com **13000×13000×9000 para todos** — ou seja, cada bioma cobrindo o mapa inteiro, sobre a floresta que já existia.
3. `set_pcg_component_properties` com `GenerateOnLoad` em todos.
4. `generate_pcg` × 4 → ~4× a vegetação, sem nenhuma exclusão das casas.

Nenhuma ferramenta reclamou, porque **nenhuma delas olhava para o resto do nível**. Três falhas estruturais:

- **Sem noção de sobreposição/orçamento**: cada ator PCG era criado no vácuo.
- **Sem exclusão**: o grafo não sabia que casas e estradas existiam; PCG não evita o que não vê.
- **`spawn_pcg_actor` ignorava os bounds do chamador** e recentralizava tudo no landscape — por isso todas as camadas viraram o mapa inteiro.

Além disso, `add_landscape_layer_info` dizia sucesso mas registrava camadas com nome `None`, então `world_landscape_paint_layer` falhava 4 vezes seguidas ("Layer 'Grass_Uncut' has no LayerInfo") — daí a ausência do terreno de grama.

## 2. Exclusão: vegetação não entra mais em casa

Nova ferramenta `set_pcg_exclusion` (umbrella `pcg`):

```
set_pcg_exclusion(graph_path, world_collision?=true, actor_tags?[], actor_classes?[],
                  spline_tags?[], spline_width?=600, margin?=0.15, target_nodes?[])
```

Para cada nó Spawner do grafo ela insere um nó **Difference**: a entrada original vira `Source`, e cada fonte de exclusão entra em `Differences`.

| Fonte | Nó criado | Para quê |
|---|---|---|
| `world_collision` | World Query (ignora o landscape, **não** ignora hits de PCG) | Buraco em volta de tudo que tem colisão — incluindo casas instanciadas por outro ator PCG |
| `actor_tags[]` / `actor_classes[]` | DataFromActor + BoundsModifier (escala `1+margin`) | Praças, POIs, atores específicos |
| `spline_tags[]` + `spline_width` | DataFromActor → SplineSampler (Distance) → BoundsModifier | Corredor livre ao longo de estradas e rios |

É **idempotente**: os nós criados levam um marcador `[AxivorExclusion:...]`, então chamar de novo reaproveita o Difference existente em vez de duplicar.

## 3. Orçamento e sobreposição

- `get_pcg_level_summary` (leitura): lista todos os atores PCG do nível com bounds, grafo, trigger, `bGenerated`, componentes ISM, instâncias, e **sobreposições** com fração de área (a partir de 25%). Traz `total_instances`, `budget`, `over_budget` e avisos em português claro do tipo "PCG_Biome_Pine sobrepõe PCG_Biome_Autumn em 100% — camadas empilhadas multiplicam a densidade".
- `generate_pcg` agora **recusa** gerar quando o nível já está acima de 2× o orçamento (a menos de `force_over_budget:true`) e sempre devolve `level_total_instances`, `budget`, `over_budget`, `overlapping_actors[]`.
- Orçamento configurável em **Settings → Ferramentas → PCG instance budget** (padrão 60000, chave ini `PCGInstanceBudget`).
- `spawn_pcg_actor` respeita os bounds e a localização que você passar (só cai no landscape quando nada foi informado) e avisa quando o novo ator sobrepõe outro com o mesmo grafo em ≥50%. `set_pcg_actor_bounds` também devolve `overlaps[]`.
- `set_pcg_component_properties` avisa quando o trigger vira `GenerateOnLoad`.

## 4. Biomas por zona, não por mapa inteiro

`world_build_biome` ganhou:

- `replace?=false` — encontrar atores `PCG_<prefixo>_*` já existentes agora **falha** com a lista deles, em vez de empilhar. Com `replace:true` apaga e reconstrói.
- `zone?{center:{x,y}, extent:{x,y}}` — a camada cobre só aquela área (alternativa ao `bounds_actor`).
- `exclude?{world_collision?, actor_tags?[], actor_classes?[], spline_tags?=["AxivorRoad"], spline_width?, margin?}` — por padrão já exclui colisão e estradas; chama `set_pcg_exclusion` em cada camada antes de gerar.
- Trigger padrão `GenerateOnDemand` (nada de regenerar o mundo a cada load).

## 5. Estradas que limpam o caminho

`world_build_road` agora marca a spline com as tags `AxivorRoad` e `Road`, e ganhou:

- `clear_vegetation?=true` — depois de construir, procura os atores PCG que cruzam a estrada, aplica `set_pcg_exclusion` com a spline (largura × 1,3) e regenera. É isto que tira as árvores de cima da pista.
- `flatten_terrain?=false` — chama a nova `world_landscape_flatten_spline`.

Nova `world_landscape_flatten_spline(spline_actor, width?=600, falloff?=0.5, sample_step?, landscape_label?, edit_layer?, height_offset?=0)`: amostra a spline, achata um disco por amostra com mistura pelo peso máximo (discos sobrepostos não brigam entre si), com uma única leitura/escrita de heightmap e transação.

## 6. Camadas de terreno que realmente pintam

- `add_landscape_layer_info` reescrito: usa `AddTargetLayer`/`UpdateTargetLayer`, chama `UpdateLayerInfoMap()` para o nome aparecer de verdade em `Info->Layers`, salva o asset criado e devolve `known_layers[]`. Aceita `save_path?`.
- Se o material já declara a camada com `LayerInfoObj` nulo, o info novo é anexado a ela em vez de criar uma duplicata.
- `world_landscape_paint_layer` se **auto-corrige**: quando a camada não está registrada mas o material a declara, ele chama `add_landscape_layer_info` sozinho e continua (`self_healed_layer_info` no resultado). Só falha quando o material realmente não tem a camada — e aí lista os nomes que o material tem.
- Reclassificada de Destructive para **Write** (roda em transação; ser destrutiva disparava diálogos que travavam a IA).

## 7. Confirmação que decide sozinha

O log mostrava `DESTRUCTIVE OP BLOCKED: 'world_landscape_paint_layer' — another confirmation is in flight` e `delete_actor` bloqueado do mesmo jeito: o slot único de confirmação estava ocupado e a chamada morria na hora.

Agora:

- O pedido de confirmação **espera** o slot livre em vez de falhar de imediato.
- Se ninguém responde dentro da janela (padrão 25 s, ajustável em Settings → **Segundos para esperar sua resposta**), o gate **decide sozinho**: em Auto Edit segue em frente; nos outros modos pula a ferramenta e explica à IA para escolher outro caminho — nunca fica travado.
- Toggle **"Decidir sozinho quando não houver resposta"** (padrão ligado) e as mensagens de erro agora dizem à IA o que fazer em vez de mandar "tente de novo em breve".
- Vale para os dois caminhos: diálogo nativo do widget e pipeline MCP.

## 8. Screenshot que existe de verdade

`take_viewport_screenshot` só armava `TakeHighResScreenShot()` e respondia `success:true` na hora — por isso a IA relatou "reporta sucesso mas nada é escrito em Saved/Screenshots". Agora ele força o redraw, lê os pixels do viewport, grava o PNG e só responde depois de confirmar o arquivo, devolvendo `file_path`, `width`, `height`, `bytes`.

## 9. Receita nova (workflow `open_world`)

1. `get_pcg_level_summary` — ver o que já existe antes de somar qualquer coisa.
2. `create_landscape` → `add_landscape_layer_info` (uma por camada) → `world_landscape_paint_layer` por regra de altura/inclinação.
3. `world_build_road(clear_vegetation:true, flatten_terrain:true)` — estradas **antes** da vegetação.
4. `world_build_biome` **uma zona por bioma** (`zone` ou `bounds_actor`), `replace:true` para refazer, exclusão ligada.
5. `get_pcg_level_summary` de novo — `total_instances` abaixo do orçamento e nenhuma sobreposição acima de 25%.
6. `take_viewport_screenshot` para conferir de olho.

## 10. Como consertar o mapa que já está travando

No chat do Axivor, dentro do projeto afetado:

1. `get_pcg_level_summary` — vai listar os 4 atores sobrepostos e o total de instâncias.
2. Apague as camadas duplicadas (`delete_actor` nos `PCG_Biome_*` que sobram), deixando uma por região.
3. `world_build_road(spline_actor:"Road_Main", clear_vegetation:true, flatten_terrain:true)`.
4. `world_build_biome(preset:..., zone:{...}, replace:true)` por região.
5. `get_pcg_level_summary` para confirmar.
