# Fase 1 — Relatório Técnico: State of Decay: Year-One Survival Edition (Steam)

Data da análise: 2026-11-01
Instalação analisada: `C:\Program Files (x86)\Steam\steamapps\common\State of Decay YOSE`
Steam AppID 329430 · buildid **845221** · `StateOfDecay.exe` FileVersion **15.11.3.5751** (timestamp PE 2015-11-04)

Convenção usada neste documento:
- **[FATO]** — verificado diretamente nos arquivos da instalação (comando/script reproduzível descrito na seção 9).
- **[FONTE]** — afirmado por fonte externa (link indicado); não verificado localmente.
- **[HIPÓTESE]** — inferência plausível que ainda precisa ser provada em execução.

Nenhum arquivo do jogo foi modificado. Toda a análise foi somente leitura; extrações foram feitas para uma pasta temporária fora do jogo.

---

## 1. Engine e arquitetura do executável

| Item | Achado | Status |
|---|---|---|
| Engine | CryEngine 3 (strings `CryEngine`, `CrytekApplication`, `CCryAction`, `CryRenderD3D11.dll`, cabeçalhos "Crytek Source File" nos Lua) | [FATO] engine; versão menor exata (3.x) [HIPÓTESE] |
| Arquitetura | **PE32, x86 (32 bits)**, machine `0x14c` | [FATO] |
| Base / ASLR | ImageBase `0x400000`, **ASLR desligado** (DllCharacteristics `0x8100`: NX + TS-aware, sem DYNAMIC_BASE) | [FATO] |
| Build | **Monolítico**: CryAction, CryNetwork, CryPhysics, CryAnimation, CryAISystem, renderer e código do jogo estão todos dentro de `StateOfDecay.exe` (não há `CryGame.dll`/`CryNetwork.dll`) | [FATO] (nenhuma DLL Cry na pasta; nomes `.dll` só aparecem como strings) |
| Compilador | MSVC 2012 (importa `MSVCR110.dll` / `MSVCP110.dll`) | [FATO] |
| RTTI | Praticamente desabilitada: apenas 5 type descriptors (CRI Movie + `type_info`) | [FATO] |
| DRM | **SteamStub**: seção `.bind`, entry point `0x1B5F2F0` dentro de `.bind`; `.text` com entropia 8.00 (criptografado em disco) | [FATO] |
| Renderer | Direct3D 11 (`CryRenderD3D11`), `d3d11.dll` carregado dinamicamente (não está na IAT); `d3dx11_42.dll` e `d3dcompiler_46.dll` importados | [FATO] |
| Script | **Lua 5.1.1** (string de copyright embutida) | [FATO] |
| UI | Scaleform GFx 3.3.93 (`.gfx`/`.as` em `gamedata.pak`) | [FATO] |
| Áudio | FMOD Ex / FMOD Event 4.44.34 (DLLs externas) | [FATO] |
| Vídeo | CRI Movie (`.usm`) | [FATO] |
| Input | DirectInput8 + XInput 1.3 | [FATO] |
| Rede (imports) | `WS2_32.dll` (35 funções, incluindo `WSASendTo`, `getaddrinfo`), `MSWSOCK.dll` (`AcceptEx`), `IPHLPAPI.DLL` | [FATO] |
| Steam | `steam_api.dll` 1.98 — só `SteamUser`, `SteamFriends`, `SteamUserStats`, `SteamUtils`, `SteamApps` (sem `SteamNetworking`/`SteamMatchmaking`) | [FATO] |

Consequências práticas:
- **32 bits sem ASLR** é favorável: endereços estáticos dentro do exe são estáveis entre execuções (para este build). Hooks precisam ser x86 (MinHook suporta).
- **SteamStub** impede análise estática direta do código: é necessário gerar uma cópia desempacotada com **Steamless** ([atom0s/Steamless](https://github.com/atom0s/Steamless)) para Ghidra/IDA — ou fazer dump em runtime. A cópia desempacotada é só para análise; o jogo continua rodando com o exe original.
- Sem RTTI e sem símbolos, a identificação de classes depende de strings de log/assert (abundantes — ver seção 3) e de comparação com o código-fonte público do CryEngine 3 (CryENGINE 3 Free SDK / headers do CryAction).

## 2. Arquivos do jogo

```
StateOfDecay.exe (21,8 MB)   steam_api.dll   fmodex.dll  fmod_event.dll  fmod_event_net.dll  d3dcompiler_46.dll
Engine\config.pak  shaders.pak
Game\gamedata.pak (scripts, XML de dados, UI)   animations.pak  characters.pak  objects.pak  textures.pak
     dialog.pak  sounds.pak  particles.pak  materials.pak  terrain.pak  levels.pak  videos.pak
     levels\Class3\level.pak          (mapa principal, Trumbull Valley)
     beltway\BeltwayC.pak / BeltwayU.pak (DLC Lifeline)   sandbox\BreakdownC.pak (DLC Breakdown)
     UserConfig.cfg
USER\<steamid64>\*.ulsave  UserStats*.cfg     USER\Profiles\default\*.xml
```

**Formato `.pak`** [FATO]: ZIP padrão, **sem criptografia**. Particularidades:
- Entradas "deflate" (método 8) contêm um fluxo **zlib com cabeçalho** (`0x28 0x91…`), não deflate cru — o `unzip` comum falha, mas `zlib.decompress(dados, 15)` funciona.
- Alguns nomes no cabeçalho local usam `\` enquanto o diretório central usa `/` (o `zipfile` do Python rejeita; é preciso ler por offset).
- Ferramentas da comunidade para isso já existem: **SoDET / SoDTools** [FONTE] (ver seção 6).

**Conteúdo relevante de `Game\gamedata.pak`** (1455 arquivos) [FATO]:
- `scripts/` — **325 `.lua` em texto puro**: `entities/actor/player.lua`, `entities/items/*` (449), `entities/vehicles/*` (72), `entities/ul/*` (entidades próprias da Undead Labs: `homesite`, `stockpile`, `ulzombiespawneditor`, `uldoor`, `ulbarricade`…), `entities/multiplayer/*` (resquícios do CryEngine: `spawnpoint`, `flag`, `perimeter`…), `ai/*`, `gamerules/singleplayer.lua`, `network/*.xml`.
- `libs/class3/` — dados de design em pares `*.xml` + `*.win.bmd` (BMD = versão binária "ULBinaryMetadata" do mesmo XML): `rts/*` (comunidade, personagens, instalações, eventos), `contentmanager/*` (missões, cenas, atividades, veículos), `zombies/*` (tipos/variantes), `items/items.xml`, `community/tasks.xml`, `map/*`.
- `libs/statemachines/*.sm`, `libs/animationgraphtemplates/*`, `libs/ui/*.gfx` (Scaleform).

**Saves** (`*.ulsave`, ~64–70 KB) [FATO]: entropia 8.00 do primeiro ao último byte, sem cabeçalho legível → **comprimido e/ou criptografado**. Formato desconhecido. Strings do exe (`RTSSaveDataBuffer::WriteBytesInternal`, `Binary saveload`) indicam serialização binária própria da camada RTS [FATO quanto às strings; HIPÓTESE quanto ao papel].

## 3. Sistemas do jogo (mapeados por strings/nomes de arquivo-fonte no exe e pelos scripts)

O exe contém ~120 nomes de arquivos-fonte em mensagens de assert/log [FATO]. Agrupados:

| Sistema | Evidência | Observação |
|---|---|---|
| Personagens / ator | `ULActor.cpp` (`ULActor::SetHealth`, `CreateStateMachine`, `QueueCollisionEvent`), `Player.cpp`, `PlayerMovementController.cpp`, `PlayerRotation.cpp`, `Character.cpp`, `scripts/entities/actor/player.lua` | Ator próprio da UL construído sobre `CActor` do CryAction. |
| Animação | CryAnimation + **Animation Graph** (`AnimationGraphState.cpp`, `ULAGEnterStateMachine.cpp`, `animations.pak`: 3458 `.caf`, `.lmg`, `.ag`, `.animevents`) | AG = sistema de estados de animação do CryEngine 3 (pré-Mannequin). |
| Máquinas de estado UL | `StateMachine2.cpp`, `SmActions/SmTriggers/SmBlackboard/SmFilters…`, `libs/statemachines/*.sm` | Comportamento de gameplay dirigido por dados. |
| IA (humanos) | CryAISystem (`CScriptBind_AI`, goalpipes, TPS, selection trees em `scripts/ai/*`), `PerceptionManager.cpp` | |
| **Zumbis** | `ZombieManager.cpp` (`NSZombieManager::Zombie::*`, `m_liveZombies[]`, `Horde::HORDE_FLAGS_ACTIVE`, `ZOMBIE_FLAG_CRY_BEHAVIOR`, `zombie_spawns_%s.markup`), `libs/class3/zombies/*` | Gerenciador **próprio**, com array de zumbis "vivos" e hordas; a flag `CRY_BEHAVIOR` sugere que só alguns zumbis usam a IA completa do CryEngine [HIPÓTESE]. É o sistema mais crítico para co-op. |
| Física | CryPhysics (`CPhysicalEntity::SetParams(pe_params_*)`, `CPhysicalProxy`, ragdoll, `Dismember.cpp`) | |
| Veículos | Sistema de veículos do CryAction (`CVehicleMovementArcadeWheeled`, `VehicleSeatAction*`, `VehicleDamageBehavior*`, XMLs em `scripts/entities/vehicles/implementations/xml`) + `VehicleManager.cpp`, `Vehicle.cpp`, `VehicleComponent.cpp` da UL | O sistema de veículos do CryAction foi projetado com suporte a rede; a camada UL por cima, não sabemos. |
| Inventário / itens | `Inventory.cpp`, `ItemData.cpp`, `ItemHelpers.cpp`, `UIMPlayerInventory.cpp`, `libs/class3/items/items.xml`, `scripts/entities/items/*` | Itens próprios da UL; mochila por personagem. |
| Comunidade / base ("RTS") | `RTSWorld.cpp`, `RTSData.cpp`, `RTSActions.cpp`, `Community.cpp`, `Home.cpp`, `Facility.cpp`, `Fate.cpp`, `libs/class3/rts/*` | Simulação estratégica abstrata (moral, recursos, enclaves, eventos "fate"). |
| Missões / eventos | `ContentManager.cpp`, `CmMission.cpp`, `CmObjective.cpp`, `CmScene.cpp`, `Activity.cpp`, `Scene.cpp`, `contentmanager/missions.xml`, `scenes.xml`, `activities.xml` | |
| Mundo / markup | `Markup.cpp`, `MarkupTool.cpp`, `level.pak` com `.markup` (30) | Pontos de spawn, busca, etc. |
| Save | `SaveGameStrings.cpp`, `RTSSaveDataBuffer`, `CCryAction::SaveGame` | |
| Script binds | `CScriptBind_*`, `ULGame_ScriptBindScenes.cpp`, Lua 5.1.1 | Lua pode chamar funções nativas expostas. |

### 3.1 Achado central: a pilha de rede do CryEngine está compilada no exe [FATO]

Strings encontradas na `.rdata`:
- Classes de rede: `CNetChannel`, `CNetContext` (e membros `m_aspectNames`, `m_channelSubscriptions`, `m_changeBuffer`…), `CCryLobby`, `CCryMatchMaking::SessionDisconnectRemoteConnection`, `CClientContextView`.
- **Comandos de console**: `connect`, `disconnect`, `listen`, `status`.
- **~200 cvars** `net_*`, `sv_*`, `cl_*`: `sv_port`, `sv_maxplayers`, `sv_gamerules`, `sv_password`, `sv_lanonly`, `sv_bind`, `net_lobby_default_port`, `net_inactivitytimeout`, `net_phys_lagsmooth`, `cl_bandwidth`…
- Gameplay MP: `g_multiplayerEnableVehicles`, `"Vehicles disabled in multiplayer, not spawning entity '%s'"`, `CGameRules` RMIs (`RMI:CGameRules:ClEnteredGame`, `ClChatMessage`…), `CUIMultiPlayer`.
- Dados de rede do CryNetwork distribuídos com o jogo: `scripts/network/compressionpolicy.xml`, `scheduler.xml`, `entityscheduler.xml`, `wrld.xml`.
- Scripts Lua com `Net.Expose` (13 arquivos, incluindo `player.lua` com RMIs `Revive`, `MoveTo`, `AlignTo`, `ClearInventory`), `Client.On*`/`Server.On*`, `RMI` em minas.
- `scripts/gamerules/singleplayer.lua` define `SinglePlayer:IsMultiplayer() return false`.

Interpretação:
- No CryEngine 3, **o single-player já roda como servidor + cliente locais** ligados por um canal local (arquitetura do CryAction). Isso explica por que a camada de rede está presente mesmo num jogo SP. [FONTE: arquitetura conhecida do CryEngine 3/CryAction; HIPÓTESE de que SoD segue o padrão — consistente com as strings]
- **[HIPÓTESE crítica, não provada]**: a camada de **gameplay da Undead Labs** (ULActor, ZombieManager, RTS, ContentManager, Inventory) provavelmente **não** implementa serialização de rede (`NetSerialize`/aspects/RMIs próprias). O CryEngine replicaria entidades "genéricas" (posição de entidades, física), mas os sistemas UL ficariam divergentes entre máquinas.
- **[FONTE, não verificado]** um usuário da comunidade afirma que "o engine não suporta multiplayer, com clientes sendo derrubados a cada 2 minutos" ([Steam](https://steamcommunity.com/app/329430/discussions/0/282992646970088299)). Não há detalhes técnicos que confirmem nem refutem.

Isso abre **duas estratégias distintas** (detalhadas na seção 7) e precisa ser testado cedo, pois muda todo o projeto.

## 4. Como os estados do jogo são armazenados

| Estado | Onde | Status |
|---|---|---|
| Definições de design (itens, zumbis, missões, comunidade) | XML + BMD em `gamedata.pak` | [FATO] |
| Estado de runtime de entidades | Entity System do CryEngine (IDs de entidade 32 bits; pools via `EntityPoolDefinitions.xml`) | [FATO] strings/arquivos; layout em memória desconhecido |
| Zumbis vivos | `NSZombieManager` com `m_liveZombies[]` (índice + flag active) | [FATO] strings; estrutura [HIPÓTESE] |
| Comunidade / mundo persistente | Camada RTS (`RTSWorld`, `RTSData`, `RTSHandle`) | [FATO] strings |
| Progresso salvo | `USER\<steamid>\*.ulsave` (Steam Cloud ativo) — binário opaco | [FATO] |
| Config | `Game\UserConfig.cfg`, `USER\Profiles\default\*.xml`, `Engine\config.pak` | [FATO] |

## 5. Pontos de integração identificados

1. **Injeção de DLL nativa x86** (proxy de DLL carregada pelo jogo — p.ex. `dinput8.dll`, `xinput1_3.dll` ou `version.dll`, todas importadas) — permite rodar código no processo sem alterar o exe. [FATO quanto aos imports; técnica padrão]
2. **Interfaces do CryEngine** via `gEnv` / `ISystem` → `IEntitySystem`, `IGameFramework`, `IConsole`, `IScriptSystem`, `IActorSystem`, `IVehicleSystem`. Localizar o ponteiro global `gEnv` no exe desempacotado é o primeiro passo de RE. [HIPÓTESE: padrão CryEngine 3; precisa ser localizado]
3. **Console do CryEngine** (`IConsole::ExecuteString`) — o mod da comunidade "MultiTool Console Runner" (Nexus #410) afirma enviar comandos ao console e funcionar no YOSE [FONTE]. Se confirmado, dá acesso a cvars/comandos `listen`/`connect`.
4. **Lua** — scripts em texto puro; a comunidade substitui arquivos soltos na pasta `Game\` [FONTE]. Permite adicionar lógica de alto nível (com limites: só o que os script binds expõem).
5. **Arquivos de dados** (XML/BMD) para, por exemplo, definir um personagem/arquétipo para o jogador remoto.
6. **Rede própria** via Winsock/ENet/Steam Networking dentro da DLL injetada (independe do CryNetwork).

## 6. Ferramentas de modding existentes para a versão Steam

| Ferramenta | Função | Status / compatibilidade |
|---|---|---|
| **SoDET / SoDTools** | Desempacotar `gamedata.pak`, `BeltwayC/U.pak` | [FONTE] [Steam](https://steamcommunity.com/app/241540/discussions/0/364042262872998952), [Nexus #522](https://www.nexusmods.com/stateofdecay/mods/522?tab=posts). Feito para a versão original; YOSE não confirmado (nosso script em Python já extrai os paks do YOSE). |
| **SODBMDEdit** | Editar arquivos `.bmd` | [FONTE] mesmo tópico; recomendado só para editar dados existentes. |
| **MultiTool Console Runner (Phacops)** | God mode, spawn/kill zumbis, posição, **envio de comandos ao console** | [FONTE] [Nexus #410](https://www.nexusmods.com/stateofdecay/mods/410); um usuário diz que funciona no YOSE ([Steam](https://steamcommunity.com/app/329430/discussions/0/1489987633995789927)). Página não acessível daqui (HTTP 403) — técnica interna não confirmada. **Melhor referência prática** para saber como acessar o console/engine neste build. |
| Mods de dados (Nexus, Discord, grupo "State of Decay Modding Crew") | Ajustes de XML/Lua | [FONTE] muitos só funcionam na versão original. |
| Steamless | Remover SteamStub para análise estática | [FONTE] [GitHub](https://github.com/atom0s/Steamless) — suporta x86. |
| Ghidra / IDA / x64dbg (x32dbg) / Cheat Engine / ReClass.NET | RE estática e dinâmica | Genéricas. **Nenhuma está instalada nesta máquina** [FATO]. |
| Código-fonte público do CryEngine 3 (Free SDK / headers CryAction) | Referência de interfaces e layouts | Versões diferentes — usar só como guia, nunca copiar offsets. |

Não existe SDK oficial: a Undead Labs declarou que não forneceria ferramentas de mod para PC [FONTE: citado em fórum, não verificado na origem]. **Não foi encontrado nenhum mod co-op existente nem tentativa documentada de engenharia reversa para multiplayer** em SoD1 [FONTE: buscas web, tópicos Steam].

## 7. Possibilidades reais de multiplayer

### Estratégia A — Reativar o cliente/servidor nativo do CryEngine
Usar `listen`/`connect` + gamerules próprias, contando com CryNetwork para replicar entidades.
- Prós: replicação de entidades, física, veículos (CryAction) e RMIs Lua já existem.
- Contras: a camada UL (zumbis, RTS, missões, inventário) quase certamente não foi feita para isso; o jogo está fixo em `SinglePlayer`; carregar um segundo jogador exige spawn de ator remoto que o código UL não espera; possíveis crashes difíceis de depurar dentro de código sem símbolos.
- **Viabilidade: desconhecida — teste barato e decisivo.** Se `listen` num host + `connect` no cliente carregar o nível e mostrar o outro ator, ganhamos meses. Se não, descartamos com evidência.

### Estratégia B — Sincronização própria (modelo Co-op Anything / "puppet")
DLL injetada nas duas instâncias; cada jogo roda seu próprio mundo SP; uma camada de rede própria (UDP) envia estado do host; o cliente cria **entidades-fantoche** (personagem remoto) e o host é autoritativo para mundo/zumbis.
- Prós: não depende de código de rede que pode estar quebrado; controle total; compatível com SP (DLL desativada = jogo normal).
- Contras: cada sistema (zumbis, combate, inventário, missões) exige RE e hooks próprios; zumbis do cliente precisam ser suprimidos e substituídos por espelhos do host.
- **Viabilidade do primeiro marco (dois jogadores vendo um ao outro): alta**, desde que se consiga (1) ler posição/rotação do jogador local e (2) spawnar uma entidade com modelo de personagem e movê-la — ambos operações padrão do `IEntitySystem`.

### Estratégia C (fallback) — Híbrida
B como base, usando partes de A (p.ex. RMIs/serialização) apenas se comprovadamente estáveis.

### Viabilidade por sistema (estimativa inicial — será revisada na Fase 2)

| Sistema | Dificuldade | Comentário |
|---|---|---|
| Posição/rotação de jogadores | Baixa–média | Entidade fantoche + interpolação. |
| Animações | Média–alta | Animation Graph: replicar inputs/estado do AG, não ossos. |
| Combate (dano) | Alta | Precisa hook no pipeline de hit/dano de `ULActor`/`CGameRules`. |
| Zumbis/NPCs | **Muito alta** | `ZombieManager` próprio, spawn por proximidade do jogador; com dois jogadores distantes o host precisa simular zumbis perto do cliente. |
| Inventário compartilhado | Média | Mais fácil via "estoque" (stockpile) da comunidade; mochila individual é mais simples que estado compartilhado. |
| Veículos | Média–alta | Sistema do CryAction ajuda; dirigir veículo do host pelo cliente exige autoridade transferível. |
| Interação com objetos (busca, portas) | Média | Eventos discretos; hooks em `InteractionPoints`/`ul_searchable`. |
| Missões/eventos/RTS | Alta | Manter só no host; cliente recebe resultado. Muitas cenas/diálogos assumem um protagonista. |
| Persistência | Baixa (se só o host salva) | Save opaco; o cliente não salva o mundo. |

## 8. Dificuldades técnicas previstas

1. **SteamStub** — análise estática exige cópia desempacotada; o jogo pode verificar integridade? (não observado). Atualizações do Steam mudam offsets → fixar e validar o build 845221 (checar hash do exe ao carregar a DLL).
2. **Sem símbolos/RTTI** — RE baseada em strings de log e em comparação com o SDK público do CryEngine 3.
3. **Duas instâncias na mesma máquina** — Steam e jogos CryEngine frequentemente bloqueiam segunda instância (mutex/`SteamAPI_Init`). Precisa ser testado; alternativa: dois PCs ou VM.
4. **Pasta `USER\` e saves dentro da pasta do jogo** — duas instâncias na mesma instalação disputam saves/Steam Cloud. Solução provável: cópia separada do jogo para testes (também atende à regra de backup).
5. **Mundo é "single-protagonista"**: câmera, cenas, diálogos, RTS e spawns são centrados em um jogador.
6. **Zumbis por proximidade** (streaming do mundo ao redor do jogador): o cliente distante do host terá mundo "vazio" no host.
7. **Processo 32 bits** (limite de ~2–4 GB de memória) — a DLL deve ser leve.
8. **Saves criptografados/comprimidos** — não podemos sincronizar progresso editando saves; sincronização deve ser em memória.

## 9. Reprodutibilidade

Scripts de análise usados (somente leitura, Python 3, sem dependências), guardados em `tools/analysis/` na próxima etapa se aprovado:
- `pe.py` — cabeçalho PE, seções e imports.
- `ent.py` — entropia por seção (detecção de SteamStub).
- `paks.py` / `extract.py` — listagem/extração dos `.pak` (lida com zlib e `\`).
- `lua.py` — varredura dos scripts Lua por padrões de rede.

## 10. Próximos passos propostos (aguardando aprovação)

1. Fazer **cópia de teste** da instalação (não tocar na original) e registrar SHA-256 do exe.
2. Rodar **Steamless** numa cópia do exe → abrir no Ghidra; localizar `gEnv`, `IConsole`, `IEntitySystem`, jogador local.
3. **Experimento decisivo da Estratégia A**: conseguir acesso ao console (via técnica do MultiTool ou DLL própria) e testar `listen` / `connect 127.0.0.1` com duas instâncias.
4. Testar se **duas instâncias** do jogo abrem simultaneamente.
5. Com esses resultados, entregar a Fase 2 (viabilidade) e escolher A, B ou C com evidência.

---

## Anexo A — Metodologia Co-op Anything e aplicação a SoD

Fonte: [cranebeaker/coop-anything](https://github.com/cranebeaker/coop-anything) (README, AGENTS.md, `playbook/*.md`, `games/far-cry`, `games/crysis`, `.claude/skills/*/SKILL.md`). Lido via web, não clonado; nenhum código executado.

**State of Decay não tem entrada** no diretório do projeto (`data/directory.yaml`) nem pasta em `games/` → este seria um caminho "build from zero" (Path 2), não "revive".

Marcos da metodologia (`playbook/how-coop-mods-work.md`) e como se aplicam aqui:

| Marco Co-op Anything | Aplicação em SoD YOSE |
|---|---|
| 0 Recon: build, SHA-256 do exe, engine | Build 845221, exe 15.11.3.5751, CryEngine 3 x86 (feito, falta SHA-256 na cópia de teste) |
| 1 Hook point: log por frame + ler posição do jogador | DLL proxy (`dinput8.dll`/ASI) → hook no update do jogo; achar `gEnv`/jogador local |
| 2 Puppets 10–20 Hz, IA/input desligados | Entidade fantoche com modelo de personagem via `IEntitySystem` |
| 3 Autoridade: posição, vida, morte | Host autoritativo |
| 4 Estado do mundo/missões + snapshot ao entrar | ContentManager/RTS só no host, espelhado |
| 5 Transições e saves (save do host é canônico) | Cliente nunca salva o mundo |
| 6 Testes automáticos com perda/latência | `tools/chaos`, `tools/flight`, `tools/gamepool` (instalações isoladas) |

Diretrizes do projeto que adotaremos:
- **Usar o gancho de mais alto nível possível**: primeiro multiplayer/script nativo da engine (→ nossa Estratégia A / Lua), depois hooks inline (MinHook) só onde necessário.
- **Precedente CryEngine mais próximo — Far Cry co-op** (`games/far-cry/README.md`, CryEngine 1, 32 bits): mod em Lua com um tipo de jogo `COOP` que carrega as regras single-player **+** um `.asi` (Ultimate ASI Loader) que corrige o `CryGame.dll` por endereço. Ou seja, **o caminho "reaproveitar a rede do CryEngine + patches nativos" já funcionou em outro jogo da família**, com problemas típicos documentados: estouro de banda com muitas entidades, gatilhos de área que só disparam para `IsMyPlayer()`, IA dormindo no servidor, saves que contornam o fluxo padrão. Esperamos problemas equivalentes em SoD (zumbis por proximidade, cenas centradas no jogador).
- Transporte: ENet ou GameNetworkingSockets; Steam relay via AppID 480 (Spacewar) como opção para não exigir port forwarding, mantendo IP direto/LAN como base. Movimento nunca em canal reliable-ordered; IDs estáveis, nunca ponteiros; keep-alive.
- "Copiar resultados, não eventos" (ex.: zumbi morto no host → estado morto no cliente, sem reexecutar a lógica de morte).
- Regras: nunca tocar na instalação/saves do dono (trabalhar em cópias); registrar SHA-256 antes de usar offsets; marcar desconhecidos como `unknown`; testar com bots/duas instâncias reais.
- Limitações declaradas pelo próprio projeto: diálogos e cutscenes são "não resolvidos em geral"; saves são frágeis; binários fechados sem SDK são o caso mais difícil.

Observação: a mesma análise sugere que nossa Estratégia A deve ser testada primeiro, exatamente como o projeto Far Cry fez — se a rede do CryEngine 3 estiver utilizável, o trabalho vira "corrigir o que a UL não fez para rede"; se não, seguimos a Estratégia B (puppets via DLL).
