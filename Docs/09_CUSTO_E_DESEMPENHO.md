# Axivor AI — Custo de tokens e desempenho

## Catálogo enxuto de ferramentas
`[BpGeneratorUltimate] LeanToolCatalog` no ini global (padrão `True`). Vale para o Architect e para o servidor MCP.

- Ligado: os modelos recebem só nome, descrição e `{action}` de cada umbrella. Ação e parâmetros vêm sob demanda por `search_tools` e `get_tool_docs`.
- Desligado (`False`): volta o schema completo derivado dos metadados. O modelo erra menos parâmetros na primeira tentativa, mas cada requisição fica muito maior.

Reinicie a sessão de um cliente MCP externo depois de mudar a chave, porque ele guarda a lista de ferramentas.

## Medição no log
Em `Saved/Logs/<Projeto>.log`:

- `[Architect][usage] <provedor> round=...`: tokens de entrada, cache lido e escrito, saída, raciocínio, tamanho da requisição e tempo do modelo em cada rodada.
- `[Architect][usage] <provedor> total ...`: soma do pedido, com o tempo do modelo (`model_s`) e o tempo entre rodadas, gasto com ferramentas (`tool_s`).
- `[Architect][tool] <ferramenta> took N ms`: ferramentas que levaram 100 ms ou mais.

Agentes ACP (Gemini CLI, Antigravity, Codex...) têm seu próprio loop: deles só aparece `[Architect][tool]`.

## Cache de prompt (Claude)
Além do bloco de sistema, as duas últimas mensagens de usuário recebem `cache_control`. Os demais provedores não recebem marcação: o log só mostra o cache que eles informam.

## Contador da conversa
`input` soma todas as rodadas e, no Claude, inclui cache lido e escrito. Antes o contador só somava a última rodada, então os totais agora parecem maiores, mas estão corretos.
