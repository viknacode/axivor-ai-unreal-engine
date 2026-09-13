# Axivor AI — Modo Turbo e toolsets nativos do UE 5.8

Data: 2026-09-07 (terceira rodada)

## 1. O que o UE 5.8 oferece (pesquisa)

Fontes: documentação oficial "Unreal MCP in Unreal Editor" (Epic Developer Community) e o código dos plugins no engine instalado (`Engine/Plugins/Experimental/...`, `Engine/Plugins/Developer/Sandbox/FileSandbox`).

| Recurso | O que é | Estado |
|---|---|---|
| **Unreal MCP** (`ModelContextProtocol`) | Servidor MCP embutido no editor, HTTP + SSE em `http://127.0.0.1:8000/mcp`, só loopback, sem autenticação. Console: `ModelContextProtocol.StartServer [port]`, `ModelContextProtocol.GenerateClientConfig <ClaudeCode|Cursor|VSCode|Gemini|Codex|All>` (gera `.mcp.json` na raiz do projeto). Preferências em *Editor Preferences → General → Model Context Protocol* (`bAutoStartServer`, `ServerPortNumber`, `ServerUrlPath`, `bEnableToolSearch`). | Experimental |
| **Toolset Registry** (`ToolsetRegistry`) | A camada de ferramentas que o MCP serve. Qualquer `UToolsetDefinition` com `UFUNCTION(meta=(AICallable))` vira ferramenta com JSON schema. API C++: `UToolsetRegistrySubsystem::Get()->ToolsetRegistry` com `ForEachToolset`, `Find`, `GetJsonSchema`, `ExecuteTool("Toolset.Tool", json)`. Permissões: *Editor Preferences → Plugins → Toolset Registry* (`ToolsetBlockedNames`, `ToolsetAllowedNames`, `AgentSkillBlockedNames/AllowedNames`, `SetObjectPropertiesBlockedClasses/Properties`). | Experimental |
| **All Toolsets** (`AllToolsets`) | Meta-plugin que liga 21 toolsets: AIModule, AnimationAssistant, AutomationTest, ConfigSettings, Conversation, DataRegistry, DataflowAgent, Editor (EditorApp + Logs), GameFeatures, GameplayTags, GAS (AbilitySystemInspector, AttributeSet, GameplayCue), MCPClient, Niagara, PCG (+PCGSpatial), Physics (PhysicsAsset), Plugin, SemanticSearch, SlateInspector, StateTree, UMG, WorldConditions. Fora do pacote: LiveCoding, MetaHumanGenerator, SequencerAnimMixer, ChaosClothAsset, MVVM. | Experimental |
| **Agent Skills** | `UAgentSkill` (DataAsset com descrição + instruções) e `UAgentSkillToolset` (`ListSkills`, `GetSkills`, `CreateSkill`, `UpdateSkill`) — skills reutilizáveis que o agente lê antes de agir. | Experimental |
| **File Sandbox** (`FileSandbox`, módulos `FileSandboxCore` + `FileSandboxUI`) | Gravações de assets/arquivos capturadas num sandbox nomeado; `FGlobalSandbox::Enter/Leave/GetChanges/Persist/Discard/DiscardFiles`. É o mecanismo de "permissão para modificações": a IA trabalha livre e o humano aplica ou descarta o lote. | Developer |

Conclusão da pesquisa: o MCP nativo **não** tem modo somente-leitura, allowlist por ferramenta nem confirmação de operações destrutivas. O controle fica no Toolset Registry (bloqueio por nome) e no File Sandbox (reversão em lote). Quem quer "poder total com rede de segurança" combina os dois — foi o que implementamos.

## 2. O que foi implementado

### 2.1 Extensão `EngineToolsets` (umbrella `engine`)
`Source/Extensions/UECPEngineToolsetsExt/` — módulo Editor carregado pelo sistema de extensões do Axivor (aparece em *Settings → Extensions*, ativo por padrão). Chama o Toolset Registry **em processo**: não precisa do servidor HTTP e passa pelos mesmos modos de interação/confirmações das outras ferramentas.

| Tool | Função |
|---|---|
| `engine_list_toolsets(filter?, include_tools?)` | Catálogo de toolsets + nomes das ferramentas |
| `engine_describe_toolset(toolset)` | JSON schema (parâmetros) das ferramentas de um toolset |
| `engine_call_tool(toolset, tool \| name="Toolset.Tool", input{}, wait_ms?)` | Executa; devolve resultado ou `job_id` se demorar |
| `engine_call_tool_result(job_id)` | Poll de chamadas longas (testes de automação etc.) |
| `engine_sandbox(action=status\|enter\|changes\|persist\|discard\|leave, files?[])` | File Sandbox |
| `engine_mcp_server(action=status\|start\|generate_client_config, port?, client?)` | Servidor MCP nativo para agentes externos (Claude Code, Cursor…) |

Documentação para a IA em `Docs/engine.md` (entregue via `get_tool_docs(category='engine')`). Classificação de segurança: list/describe/result = leitura; call/sandbox/mcp_server = escrita.

### 2.2 Modo Turbo (Settings → AI Behaviour → card "Modo Turbo")
Chaves `TurboMode` e `TurboSandbox` em `[BpGeneratorUltimate]` (ini global). Efeitos com Turbo ligado:
- Architect: sem confirmação nativa para ferramentas destrutivas (`delete_*`, `clear_*`, `move_asset`…) e sem prompt para ferramentas de servidores MCP externos, em Auto Edit.
- Agentes ACP (Claude Code, Codex, Gemini…): todo pedido de permissão é aprovado automaticamente (exceto em *Só Chat*) e a sessão é aberta em modo bypass/auto-edit.
- "Só Chat" continua somente leitura; "Plan" continua bloqueando ferramentas de escrita até `proceed_with_plan`.
- Com **TurboSandbox** ligado, o editor entra no sandbox `AxivorAI` ao carregar a extensão. A barra de status mostra `Sandbox N` com **Aplicar / Descartar / Sair**; a paleta (Ctrl+K) tem os comandos de sandbox.

### 2.3 Plugin
`BpGeneratorUltimate.uplugin` agora depende de `ToolsetRegistry`, `AllToolsets` e `FileSandbox` (só Editor). Na primeira abertura o Unreal pergunta para ativar esses plugins no projeto; aceite e reinicie.

### 2.4 Arquivos tocados
- Novos: `Source/Extensions/UECPEngineToolsetsExt/{Build.cs, .uecpext.json, Public/…Module.h, Private/…Module.cpp, Docs/engine.md}`
- `BpGeneratorUltimate.uplugin` (módulo + dependências)
- `UECPArchitect/Private/FUECPArchitectCoordinator.cpp`, `UECPACP/Private/FUECPAgentRunnerCoordinator.cpp` (bypass Turbo)
- `UECPShell/Private/Widget/UUECPSettingsBridge.cpp` (chaves), `UECPShell/Public|Private/Widget/UUECPAppBridge.*` (`SandboxCommand`, `PushSandboxState`)
- `UECPCore/Private/Managers/SettingsManager.cpp` (migração das chaves)
- `Resources/UI/settings_ui.html` (card Turbo), `Resources/UI/app_shell.html` (chips Turbo/Sandbox + comandos)

## 3. Como usar
1. Abra o editor, aceite ativar os plugins, reinicie.
2. Settings → AI Behaviour → **Modo Turbo**: ligue "Ativar Modo Turbo" e, recomendado, "File Sandbox".
3. No chat, peça o que quiser. Para recursos além dos umbrellas do Axivor, a IA usa `engine_list_toolsets` → `engine_describe_toolset` → `engine_call_tool`.
4. Quando terminar uma rodada, use **Aplicar** (grava no projeto) ou **Descartar** (reverte tudo) na barra de status.
5. Para deixar o Claude Code/Cursor dirigir o editor por fora: `engine_mcp_server(action='start')` e `generate_client_config`.

## 4. Limites conhecidos
- Toolsets e MCP são experimentais no 5.8; nomes/schemas podem mudar em 5.9.
- Ferramentas do registry que precisam de vários ticks do game thread voltam como `pending`; a IA faz poll com `engine_call_tool_result`.
- O File Sandbox captura gravações via o sistema de arquivos do editor; operações que gravam fora dele (ex.: processos externos, `git`) não são capturadas.
