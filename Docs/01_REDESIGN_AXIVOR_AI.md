# Axivor AI — Redesign e nova identidade do plugin

Data: 2026-09-07
Plugin: `Plugins/UltimateEngineCopilot_UE5.8` (nome interno do plugin continua `BpGeneratorUltimate` — ver "Decisões")

## O que mudou

### Identidade
- Nome visível: **Axivor AI** (aba do editor, janela, menu Window, botão da toolbar, `.uplugin`, tela de boas-vindas, painel Info).
- Logotipo novo: chevron "A" com órbita e núcleo, gradiente violeta → ciano.
  - `Resources/Icon128.png` (ícone do editor), `Resources/bplabs1.png` (brush "BrandingLogo"), `Resources/AxivorLogo512.png` (referência).
  - O mesmo símbolo está inline em SVG nas três telas HTML (usa `--accent` / `--accent2`, então acompanha a cor de destaque escolhida).
- Links do fornecedor antigo (docs, Discord, marketplace, página de upgrade) foram removidos do `.uplugin`, do rodapé, do painel Info e das mensagens de suporte.

### Paleta "Aurora" (padrão)
| Token | Valor |
|---|---|
| `--bg` / `--bg2` / `--bg3` / `--bg4` | `#0a0d14` / `#0f131c` / `#151a25` / `#1c2230` |
| `--accent` / `--accent2` | `#8b7cf6` / `#22d3ee` |
| `--text` / `--text2` / `--text3` | `#eef2f7` / `#9aa6b8` / `#5b6675` |
| `--ok` / `--warn` / `--err` | `#34d399` / `#fbbf24` / `#f87171` |
| raio padrão `--r` | `12px` |

Tema claro e tema Classic foram reajustados para a mesma família de cores. A cor de destaque continua configurável; a cor secundária do gradiente (`--accent2`) é derivada automaticamente (matiz −60°) da cor escolhida.

### App shell (`Resources/UI/app_shell.html`)
- Barra superior com marca, abas em "pill" com sublinhado em gradiente, botão **Comandos** e vidro translúcido.
- Sidebar, lista de chats, bolhas de mensagem (sem borda lateral, cartões com hairline em gradiente), blocos de ferramenta, plano, toasts, diálogos, overlays, cards de Tools e painéis do Crew reestilizados.
- Compositor unificado com anel de foco em gradiente e botão de envio em gradiente.
- Rodapé virou barra de status com selo Axivor.
- **Novo: Paleta de comandos** (`Ctrl+K` ou `Ctrl+Shift+P`): navegar entre views, novo chat, escanear projeto, abrir ferramentas, Instruções, GDD, Memória, Configurações, reportar problema. `Ctrl+,` abre Configurações.
- **Novo: chips de prompt rápido** nas telas vazias do Architect e do Scanner (preenchem o compositor).
- Animação sutil de entrada das mensagens (respeita "Reduced motion").
- Toda a camada nova fica no bloco `AXIVOR AI — Design Layer` (CSS, fim do `<style>`) e `AXIVOR AI — runtime layer` (JS, fim do `<body>`), sem alterar os handlers C++↔JS existentes.

### Configurações (`Resources/UI/settings_ui.html`)
- Cabeçalho com marca, navegação lateral em pills, cards com vidro, toggles e botões em gradiente.
- **Novo card "Axivor Studio"** em *Appearance*:
  - Estilo visual: **Aurora** (padrão) · **Graphite** · **Minimal**
  - Toggles: Efeitos de brilho · Painéis translúcidos · Marca Axivor
- Presets de cor: Axivor Violet (padrão), Cyan, Pink, Copper, mais os anteriores.
- Painel *Info* com hero da marca e card de atalhos (substitui os links do fornecedor).

### Tela de boas-vindas (`Resources/UI/welcome_screen.html`)
Reescrita: hero com a marca, grade de recursos (Architect, Scanner, Crew, Tools & MCP), dica do `Ctrl+K`, seletor de idioma e botão "Começar". O contrato com o C++ (`initWelcome`, `initAnnouncement`, `setLanguage`, navegação `ue://welcome/...`) não mudou. No modo *welcome* o conteúdo remoto é ignorado (só anúncios usam conteúdo remoto).

### C++ (rebuild necessário)
| Arquivo | Mudança |
|---|---|
| `BpGeneratorUltimate.uplugin` | FriendlyName/Description/CreatedBy, URLs vazias |
| `UECPShell/Private/UECPShellModule.cpp` | Títulos da aba/janela, log, JSON de fallback do welcome |
| `UECPShell/Private/UECPCommands.cpp` + `Public/UECPCommands.h` | Rótulo do comando "Axivor AI" (menu Window / toolbar) |
| `UECPShell/Private/Widget/UUECPSettingsBridge.cpp` | Accent padrão `#8b7cf6`; links vazios; chaves novas `VisualStyle`, `GlowEffects`, `GlassPanels`, `BrandMark` (load + save) |
| `UECPShell/Private/Widget/SUECPMainWidget.cpp` | Accent padrão; push inicial de aparência inclui as chaves novas |
| `UECPCore/Private/Managers/SettingsManager.cpp` | Chaves novas na lista de migração |
| `UECPShell/Private/Widget/SUECPMainWidget_Architect.cpp` | Texto de suporte sem links externos |
| `UECPShell/Private/Utils/DiagramUtils.cpp` | `ue://upgrade` não abre mais página externa |

As chaves novas são persistidas em `[BpGeneratorUltimate]` no ini global do plugin (mesmo lugar de `AccentColor`). Enquanto o C++ não for recompilado, o JS mantém um espelho em `localStorage` (quando disponível) e cai no padrão *Aurora*.

## Decisões
- O **nome interno do plugin** (`BpGeneratorUltimate`: nome do `.uplugin`, `FindPlugin(...)`, seção de config, nomes de módulos `UECP*`, style set) foi mantido. Renomear isso exigiria tocar centenas de arquivos, configs de MCP (`uecp-*`) e caminhos em `Saved/`, com alto risco e sem ganho visual.
- Cabeçalhos de copyright dos fontes foram mantidos.
- A aba **Learn** continua carregando o portal externo do fornecedor (é conteúdo, não identidade). Se quiser removê-la, apague o botão `data-view="learn"` e o `#view-learn` no app shell.
- Os nomes "Blueprints Lab 1.1 / 0.9" no modo *Free* são os nomes dos backends remotos gratuitos, vêm do servidor e não foram alterados.

## Como testar sem o editor
```bash
node "C:/Users/Victor/AppData/Local/Temp/claude/C--Users-Victor-Documents-Unreal-Projects-VRPGFramework/2ba84859-54ba-46a6-a639-1282eea00d33/scratchpad/preview_server.js" "C:/Users/Victor/Documents/Unreal Projects/VRPGFramework/Plugins/UltimateEngineCopilot_UE5.8/Resources/UI" 8765
```
Abra `http://127.0.0.1:8765/app`, `/settings` e `/welcome`. (O servidor inlines as libs `%%*_JS%%` e roda sem a ponte `window.ue`.)

## Rebuild
Com o editor aberto (Live Coding ativo):
```bash
"C:/Program Files/Epic Games/UE_5.8/Engine/Build/BatchFiles/Build.bat" VRPGFrameworkEditor Win64 Development -Project="C:/Users/Victor/Documents/Unreal Projects/VRPGFramework/VRPGFramework.uproject" -NoHotReloadFromIDE -WaitMutex
```
Depois reinicie o editor para carregar os DLLs novos. As telas HTML são lidas do disco a cada abertura da janela, então mudanças de CSS/JS não exigem rebuild.

## Backups
Originais copiados para `%LOCALAPPDATA%\Temp\claude\...\scratchpad\backup_original\` (HTML, PNGs, `.uplugin` e os `.cpp` alterados).
