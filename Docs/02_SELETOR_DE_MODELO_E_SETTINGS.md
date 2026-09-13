# Axivor AI — Seletor de modelo no chat e reforma da seção "Inteligência"

Data: 2026-09-07 (segunda rodada)

## O que mudou

### Seletor de IA/modelo no compositor
O chip ao lado de **Enviar** (antigo "slot chip") agora abre um painel com duas colunas:
- **Esquerda — provedores:** todos os perfis cadastrados, agrupados em *Agentes*, *Chaves de API* e *Grátis*, mais os agentes instalados ainda não ligados a um perfil. Clique = vira o **padrão para todos os chats**; o alfinete fixa **só nesta conversa** (equivale ao antigo override por chat).
- **Direita — detalhe:** para agentes CLI (Claude Code, Codex…) lista os **modelos** e as **opções da sessão** (raciocínio, modo rápido etc.) descobertas via ACP; para chaves de API mostra o modelo configurado e um atalho para editar; para o modo grátis, uma explicação.
- O chip mostra `Provedor · Modelo` e um alfinete quando fixado.

Contrato C++↔JS (novo, em `UUECPAppBridge`):
| Método | Efeito |
|---|---|
| `RequestModelPicker()` / `PushModelPicker()` | Envia `onModelPicker(json)` com perfis, agentes instalados e catálogo por agente (valores salvos para o slot resolvido) |
| `PickerSelectSlot(idx, bPinToChat)` | `false` → `SetActiveSlot` + remove override do chat; `true` → `SetChatSlot(idx)` |
| `PickerSetAgentOption(agent, key, value)` | Grava `[BpGeneratorUltimate.ACP.SlotN] <agent>.Model` ou `.Option.<key>` (mesmas chaves do Settings), espelha `AgentModel/AgentEffort` no slot e para a sessão viva do chat (a próxima mensagem reinicia o agente com o novo modelo) |
| `PickerDiscoverAgent(agent)` | Dispara a descoberta de modelos (ACP) se ainda não houve |

O coordinator do ACP também chama `PushModelPicker()` sempre que captura o catálogo de um agente, então o painel atualiza sozinho após a descoberta.

### Settings → "Inteligência" (antigo "AI Models")
- Card de introdução explicando que o modelo se escolhe no chat, com um mock do chip.
- "API Key Slot" → **Perfis de IA** (cada perfil = tipo de provedor + credenciais; o padrão é o usado nos chats).
- "Provider Mode" → **Tipo do perfil**, com subtítulo em cada opção: Grátis · Agente CLI · Chave de API / LLM local.
- Textos em português nos painéis; backends gratuitos renomeados para *Axivor Cloud 1/2*; "Save Slot" → "Salvar perfil".
- Toda a mecânica existente (slots 1–9, catálogo ACP, chaves) foi mantida; só a apresentação e a cópia mudaram. O seletor de modelo/effort/fast do card do agente ativo continua no Settings como caminho alternativo.

### Nota sobre "ainda marrom"
O ini global do plugin (`%LOCALAPPDATA%\UltimateCoPilot\settings.ini`) guarda a cor de destaque escolhida anteriormente (`AccentColor`). A cor salva é aplicada por cima do tema; para voltar ao violeta Axivor, escolha o primeiro swatch em Appearance → Theme & Colors.

## Build
Compilação e link concluídos com sucesso (editor fechado). DLLs `UECPShell`, `UECPACP` e `UECPCore` atualizados em `Binaries/Win64`.
