<p align="center">
  <img src="Resources/AxivorLogo512.png" width="112" alt="Axivor AI">
</p>

<h1 align="center">Axivor AI</h1>

<p align="center">
  <b>Um copiloto de engenharia que vive dentro do editor da Unreal Engine 5.8.</b><br>
  Descreva o que você quer; o Axivor constrói, edita, verifica e depura Blueprints, C++, níveis, animação, materiais e mais — com você, dentro do editor, usando as APIs reais da engine.
</p>

<p align="center">
  <a href="#recursos">Recursos</a> ·
  <a href="#screenshots">Screenshots</a> ·
  <a href="#instalação">Instalação</a> ·
  <a href="#escolha-sua-ia">Escolha sua IA</a> ·
  <a href="#como-funciona">Como funciona</a> ·
  <a href="README.md">English</a>
</p>

---

## Recursos

**Architect** — um chat que edita o seu projeto. Modos Plan, Ask-before-edit, Auto Edit e Just Chat; lista de tarefas com verificação por tarefa (uma tarefa não pode ser marcada como concluída sem o read-back que a prova); paleta de comandos (`Ctrl+K`); prompts rápidos; screenshots do viewport que ele consegue de fato conferir.

**Mais de 1 300 ações de editor em 70 famílias de ferramentas**, expostas à IA com schemas JSON tipados:

| Área | O que a IA consegue fazer |
|---|---|
| Blueprints | Criar, ligar e compilar grafos; identidade estável de nó por GUID; snapshot → editar → diff; `compare_blueprints` estrutural de verdade |
| C++ | Criar classes, editar arquivos (UTF-8 seguro), compilar, ler erros de build |
| Construção de mundo | Presets de landscape, heightmaps, esculpir/achatar, pintura de camadas por altura/inclinação; biomas PCG **por zona** com exclusão de colisão/estradas e orçamento de instâncias; estradas e rios em splines que limpam vegetação e achatam o terreno; prefabs, edição em massa, snap e alinhamento |
| Animação | Máquinas de estado de locomoção, montages, additive, retarget IK, Motion Matching, foot IK — cada uma com relatório de compilação |
| Materiais | Grafos endereçados por GUID, transações (undo), validação que espera a compilação de shader |
| UE 5.8 | Procedural Vegetation Editor, Control Rig Physics, Direct Mesh Control, MetaHuman (Python), presets MegaLights / Lumen / Substrate, iteração rápida de physics asset, Game Animation Sample |
| Toolsets da engine | Toolset Registry e File Sandbox nativos da 5.8 (persistir / descartar) |
| Resto | GAS, Niagara, UI/UMG/MVVM, Input, Data assets, Cinemática, Áudio, Water, Veículos, Mass, Mover, Geometry Script, Game Features, Packaging, Validação, Python |

**Project Scanner** — indexa o projeto e responde perguntas sobre assets, dependências e performance, muitas vezes localmente, sem chamada de API.

**Crew** — execuções autônomas multi-agente com papéis, checkpoints e handoffs. Os checkpoints são fail-closed: o agente precisa relatar sucesso *e* mostrar evidência somente-leitura; retries, oscilação e total de tokens têm teto.

**Workflows** — receitas ordenadas por domínio (`open_world`, `locomotion_anim`, `montage`, `retarget`, `motion_matching`, `material`, `niagara`, `ui_widget`, `gameplay_ability`, `packaging`, `metahuman`, …), cada passo com a ferramenta a usar e o read-back que o verifica.

**Modo Turbo** — deixe a IA fazer praticamente tudo no projeto sem uma confirmação por passo, opcionalmente dentro do sandbox de arquivos da engine, para você revisar e persistir ou descartar o lote inteiro.

**Servidor MCP** — toda ferramenta também fica disponível para agentes externos (Claude Code, Codex, Gemini CLI, Cursor, Claude Desktop) por um endpoint MCP HTTP, com os mesmos modos de permissão.

**Nunca trava** — um pedido de confirmação que ninguém responde é resolvido pelo plugin depois de uma janela configurável (segue em Auto Edit, pula nos outros modos) e a IA é avisada do que aconteceu.

## Screenshots

<p align="center">
  <img src="Docs/screenshots/app.png" alt="Chat Architect construindo um mapa open-world com tarefas verificadas" width="100%">
  <br><sub>Architect — chamadas de ferramenta, lista de tarefas verificada e o seletor de modelo no compositor</sub>
</p>

<p align="center">
  <img src="Docs/screenshots/palette.png" alt="Paleta de comandos" width="49%">
  <img src="Docs/screenshots/settings.png" alt="Settings — perfis de IA" width="49%">
  <br><sub>Paleta de comandos (Ctrl+K) · Settings: a IA e o modelo são escolhidos no chat</sub>
</p>

<p align="center">
  <img src="Docs/screenshots/welcome.png" alt="Tela de boas-vindas" width="70%">
</p>

## Instalação

Requisitos: **Unreal Engine 5.8**, Windows 64-bit (os módulos Mac/Linux estão listados, mas só Win64 é exercitado), Visual Studio 2022 com o workload de desenvolvimento de jogos em C++.

```bash
git clone https://github.com/viknacode/axivor-ai-unreal-engine.git "<SeuProjeto>/Plugins/AxivorAI_UE5.8"
```

1. Botão direito no `.uproject` → **Generate Visual Studio project files**.
2. Compile o target do editor (ou abra o projeto e aceite o pedido de rebuild).
3. No editor: **Window → Axivor AI**. A primeira execução mostra a tela de boas-vindas; `Ctrl+K` abre a paleta.

Plugins da engine dos quais ele depende (habilitados automaticamente): PCG, ToolsetRegistry, AllToolsets, FileSandbox, ModelContextProtocol, PythonScriptPlugin, Landscape, Foliage, Water.

## Escolha sua IA

O agente e o modelo são escolhidos **no chat**, ao lado do botão Enviar — por conversa ou fixados como padrão. Três tipos de perfil:

- **Agente CLI** — Claude Code, Codex, Gemini CLI e outros agentes ACP instalados na sua máquina, com modelos e opções descobertos ao vivo.
- **Chave de API / LLM local** — Anthropic, OpenAI, Google Gemini, DeepSeek, OpenRouter, Ollama.
- **Grátis** — um backend hospedado sem chave (com limite de uso).

Os perfis ficam em **Settings → Inteligência**; o chat só mostra o que você configurou.

## Como funciona

```
Chat (CEF/HTML) ──► Shell (Slate) ──► Coordenador Architect ──► Loop do agente (Claude / OpenAI / Gemini / ACP)
                                              │
                                              ▼
                        Dispatcher de ferramentas (schemas tipados, classe de segurança, transações)
                                              │
              ┌───────────────┬───────────────┼───────────────┬───────────────┐
           UECPTools    Extensões (33)   Toolsets da engine   Receitas         Ponte MCP
```

- As ferramentas ficam agrupadas em **umbrellas** (`pcg`, `world`, `animation`, `material`, …) e cada ação declara seus parâmetros; os schemas são derivados dessas declarações para todos os provedores.
- Toda ação que modifica roda numa **transação** do editor (undo funciona) e é classificada Read / Write / Destructive; ações destrutivas pedem confirmação, a menos que o Turbo esteja ligado.
- As extensões são módulos independentes com um manifesto `*.uecpext.json`; `uecp_selftest` reporta divergências entre handlers, metadata, docs e manifestos.
- A IA recebe um bloco de **convenções do projeto** (grade, pastas, prefixos, classes padrão, plugins ativos) em todo prompt, então constrói do jeito que o seu projeto já é construído.

## Docs

A pasta `Docs/` guarda as notas de design e engenharia de cada rodada de trabalho, incluindo o post-mortem de PCG em [`08_MUNDO_PCG_ZONAS_EXCLUSAO.md`](Docs/08_MUNDO_PCG_ZONAS_EXCLUSAO.md), que explica por que camadas de vegetação empilhadas acontecem e como o modelo de zona + exclusão + orçamento evita isso. A documentação de cada ferramenta está disponível dentro do editor via `get_tool_docs` e `search_tools`.

## Status

Desenvolvido ativamente contra a UE 5.8. Windows é a plataforma testada. Espere mudanças nos parâmetros das ferramentas entre versões; o `get_tool_docs` dentro do editor é sempre a fonte da verdade.
