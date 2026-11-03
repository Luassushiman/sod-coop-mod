# Progresso do protótipo (Fase 4)

Objetivo da Fase 4 (definido pelo usuário):
1. Iniciar duas instâncias do jogo.
2. Estabelecer conexão entre elas.
3. Permitir que um jogador entre na sessão do outro.
4. Sincronizar a posição e a rotação dos personagens.
5. Exibir os dois jogadores no mesmo mundo.

Modo-alvo: **Breakdown** (ver `AMBIENTE_DE_TESTES.md`, "Decisão de escopo").

| # | Marco | Status | Evidência |
|---|---|---|---|
| 1 | Duas instâncias no mundo ao mesmo tempo | ✅ 2026-11-01 | cópias `testenv\host` e `testenv\client`, mesmo save Sandbox; confirmado pelo usuário |
| — | Ponto de hook + leitura do jogador local (Co-op Anything, marco 1) | ✅ 2026-11-01 | log `sodcoop_host.log`: EntityId, posição e yaw corretos |
| — | Hook por frame na thread principal | ✅ 2026-11-01 | `CCryAction::PreUpdate` |
| 5 (local) | Fantoche visível, movido pelo mod (sem rede) | ✅ 2026-11-01 | `AnimObject` criado via Lua segue o jogador a 2 m; confirmado pelo usuário |
| 2 | Conexão entre as instâncias | ✅ 2026-11-01 | UDP `127.0.0.1:27650`; handshake Hello/Welcome em ~12 ms, ainda no menu |
| 3 | Cliente entra na sessão do host | ✅ 2026-11-01 | host aceita 1 cliente (recusa um 2º com `Reject`) |
| 4 | Sincronizar posição/rotação | ✅ 2026-11-01 | ~16 pacotes/s por direção; idade do dado remoto com mediana de 15 ms e máximo de 79 ms (logs `sodcoop_host.log`/`sodcoop_client.log`) |
| 5 | Os dois jogadores visíveis um para o outro | ✅ 2026-11-01 | confirmado pelo usuário nas duas janelas, no Breakdown |

**Primeiro objetivo da Fase 4 concluído**, testado com duas instâncias reais do jogo na mesma máquina.

## Animação do fantoche (2026-11-01)

| Item | Status | Evidência |
|---|---|---|
| Janela sem foco continua rodando (as duas visíveis) | ✅ | `keep_running_unfocused=1`: `haveFocus` forçado no Pre/PostUpdate + `sys_SleepIfInactive=0` |
| Locomoção por velocidade: idle/andar/correr/sprint | ✅ | blend spaces `_WALK_NW`/`_RUN_NW`/`_SPRINT_NW`; limiares 0,4/2,2/4,1 m/s (calibrados pelo deslocamento real: andar ~1,1–1,4, correr ~3,0–3,7, sprint ~4,5–6,6 m/s) |
| Ritmo dos passos proporcional à velocidade | ✅ | `SetAnimationSpeed` = velocidade / nominal (1,3/3,4/5,2 m/s), limitado a 0,5×–2× |
| Movimento sem pulsos | ✅ | interpolação de snapshots com atraso de 100 ms, pelo relógio do remetente; 20 pacotes/s reais |
| Corpo sem "teleporte para trás" e girando no lugar | ✅ quase todo | compensação do deslocamento de raiz pela bacia, no PostUpdate; confirmado pelo usuário ("tá bem pouco agora") |

Bugs encontrados e corrigidos nesta etapa:
- Medidor de velocidade com `GetTickCount` (~15,6 ms) subestimava a velocidade em até 70%; trocado por `QueryPerformanceCounter` (`src/core/clock.h`).
- Taxa de envio real de 16 Hz (Sleep + intervalo); agora agendada e com `timeBeginPeriod(1)` na thread de rede.
- O fantoche seguia o último pacote com suavização exponencial, o que gerava velocidade em pulsos; trocado por interpolação.
- Compensação de raiz lida no PreUpdate tinha 1 frame de atraso: no reset do ciclo, o corpo aparecia ~3 m atrás. Agora roda no PostUpdate.

Limitações restantes da animação:
- Pequenos artefatos na troca andar↔correr (dois clipes com deslocamentos de raiz diferentes se misturando): picos de 7–12 "resets" num segundo, perto de 2,4–2,7 m/s.
- Ataques e pulos: ver "Combate — etapa 1". Ainda sem mira/tiro, agachar, veículo, carregar mochila, interação ou morte (não testados; a detecção genérica pode já cobrir parte deles).
- Direção da animação = sempre "para frente"; andar de lado/de costas (strafe) não é representado.
- Idle mostra a bacia a ~0,40 m da origem da entidade (compensado).

## Combate — etapa 1: ações visíveis no fantoche (2026-11-01)

| Item | Status | Evidência |
|---|---|---|
| Leitura das animações em execução no esqueleto do jogador | ✅ | `src/game/anim_watch.cpp`; layout em `RE_ENDERECOS.md` seção 4c; nomes lidos corretamente (`relaxed_idle_nw`, `_RUN_NW`, `BluntLight_diagonal_chop_fromright`...) |
| Detecção da "ação" (animação mais nova da camada 0 que não é locomoção/idle) | ✅ | log `acao: ... (seq N)` |
| Ação enviada pela rede (protocolo v4) e tocada no fantoche | ✅ | confirmado pelo usuário ("golpes apareceram"); log do client: 20 ações tocadas, nenhuma falha (golpes de taco, golpe no chão, pulos, queda) |

Como funciona: quem joga envia o nome da ação e um contador; o fantoche toca a animação uma vez (blend 0,1 s, ritmo 1×), suspende a troca de passo e, quando a ação termina, volta à locomoção. O nome recebido só é aceito com letras, dígitos e `_` (vai para o Lua) e é conferido com `GetAnimationLength` (o modelo do fantoche precisa ter a animação).

### Etapa 1b: arma na mão (2026-11-01) ✅

- O Lua informa o item atual (`inventory:GetCurrentItem()`) por um **canal de texto** (nome da entidade `sodcoop_text`, lido com `IEntity::GetName`).
- A DLL acha a entidade do item pelo nome e lê o modelo de 3ª pessoa (`GetStatObj(slot)` → `GetFilePath`), envia pela rede (v5) e o fantoche prende o `.cgf` no osso `weapon_bone`.
- Testado com o pé de cabra (`club_prybar.cgf`): aparece na mão e some ao trocar para os punhos; confirmado pelo usuário.

### Etapa 1c: mira, tiro e recarga (2026-11-01) ✅

- Postura com arma (idle com espingarda) e parte de cima (segurar, tiro, recarga) replicadas por camadas; confirmado pelo usuário ("apareceu a arma na mão, tem a animação de recarga").
- Mira: o jogo levanta a arma pelo IK de aim poses, que não roda no fantoche (`AnimObject`). Foram testados `SetAimIK` nativo e a aim pose como animação comum, sem efeito (documentado em `RE_ENDERECOS.md`). Solução: clipes do jogo com a arma no ombro — parado (`Story_..._StandAim`) e andando/correndo (`Test_hero_walk/run_forward_aim`); confirmado pelo usuário ("ficou tudo certo").
- Janela sem foco não pausa mais: `focus_guard` descarta `WM_ACTIVATE(WA_INACTIVE)`/`WM_ACTIVATEAPP(FALSE)`. Antes, o host abria o menu de pausa e o fantoche ficava "duro".
- `sys_MaxFPS=60` por padrão (`[debug] max_fps`), por causa dos travamentos do PC com as duas cópias no menu.

Ajustes da mira (2026-11-01):
- Catálogo das 2561 animações do modelo gravado em `sodcoop_anims.txt` (ao lado do exe) na primeira leitura de nomes.
- Mira para baixo (parado, arma longa): inclinação calculada do alvo do IK do remetente; abaixo de -15° (volta acima de -10°) usa `Story_ar02_Quarantine_Approach_Soldier_StandAim_Down`. ✅ confirmado pelo usuário com a espingarda.
- Pistola: idle `relaxed_idle_pistol` + `Gun_shoot_revolver` parado em t=0,15 na camada 1 (braço estendido). ⚠️ **Não testado** (o personagem de teste não tinha pistola). Pendente: `com_addGun Pistol_M9` foi executado no host (vai para o **estoque da comunidade**); pegar no armário de suprimentos quando o save do Breakdown tiver base e então testar.

Ferramenta de teste: com `[debug] lua_command_file=1` (padrão), o mod executa uma vez e apaga `sodcoop_cmd.lua` da pasta do jogo (thread principal, a cada 1 s). Comandos de debug úteis do jogo: `com_add*` (estoque da comunidade), `zm_spawn`/`zm_spawnFreak` (zumbis), `i_giveitem` (não chega ao inventário da UL). **Desligar antes de qualquer versão pública.**

Limitações da etapa 1c:
- Mira para cima: o jogo não tem clipe; fica reta.
- Andar de lado/de costas mirando: só há clipes "para frente"; o fantoche usa o de frente.

Limitações da etapa 1:
- **Sem dano**: o golpe é só visual. Cada instância tem os próprios zumbis; dano depende dos zumbis compartilhados.
- Arma na mão: ✅ (etapa 1b). A pose `weaponoffset_*` (camada 6) não é replicada; armas de fogo/itens com personagem (`.chr`) em vez de `.cgf` não foram testados.
- Camadas superiores (1–15: parte de cima do corpo, aditivas) não são replicadas; só a camada 0.
- A ação começa do início no fantoche (não sincroniza o tempo da animação) e respeita o atraso de interpolação (100 ms).

Limitações conhecidas da rede atual:
- Só 1 cliente, só IP direto/LAN, sem criptografia nem Steam relay.
- A interpolação (100 ms) ainda não foi testada com perda/latência reais (`tools/chaos` do Co-op Anything).
- Ainda não foram testados: desconexão no meio do jogo (timeout de 5 s), reconexão e host em outra máquina (`bind=0.0.0.0`).
- Cada instância roda o próprio mundo: zumbis, NPCs, loot e eventos **não** estão sincronizados. Só a posição/rotação dos jogadores é compartilhada.

Limitações conhecidas do fantoche atual:
- O modelo é só o corpo (`.cdf` de body); pode aparecer sem cabeça.
- O movimento usa uma chamada Lua por frame (`ExecuteBuffer` + compilação de um trecho pequeno). Basta para o protótipo; trocar por `IEntity::SetWorldTM` nativo (vtbl `+0x54`) quando o EntityId do fantoche for obtido no lado C++.

## Zumbis compartilhados — etapa 1: espelho visual (2026-11-02)

Host autoritativo, como no Co-op Anything. Código: `src/game/zombies.cpp`.

| Item | Status | Evidência |
|---|---|---|
| Listar os zumbis do host (entidades `ULZombie<n>`) com posição, direção, animação e modelo | ✅ | log `zombies: host enviando N zumbi(s)` |
| Desligar os zumbis do client (`zm_disable 1` + `zm_killAll` ao entrar no mundo; conferência a cada 10 s) | ✅ | 0 zumbis depois de andar ~165 m com `zm_disable 1` |
| Pacote `Zombies` (10 Hz, até 10 por pacote) e fantoche por zumbi no client, interpolado | ✅ | Feral: mesma posição e animação nas duas janelas, confirmado pelo usuário |
| Modelo por tipo: especiais `characters/freaks/<tipo>/<tipo>.cdf`; comuns, modelo simplificado (EigenZombie) por gênero | ✅ parcial | Feral confirmado (troca feita ao vivo); a regra nova só vale a partir do próximo reinício |

Limitações da etapa 1:
- **Sem interação**: os zumbis não atacam o client e os golpes/tiros do client não os acertam (próxima etapa: dano pelo host).
- Zumbis comuns aparecem com o modelo simplificado (as partes sorteadas pelo jogo não são replicadas).
- O host só tem zumbis perto dele: o client longe do host fica sem zumbis.
- Os saves do host e do client são diferentes (ex.: o client já tem base); o client ainda não usa o mundo do host.
- `zm_spawn` (cvar inteira) não criou zumbis nos testes; para testar usamos zumbis existentes e teletransporte (`SetWorldPos` no ator, via `sodcoop_cmd.lua`).
- `zm_playerInvisible 1` não impediu um Feral de atacar.

## Zumbis — etapa 2a: dano do client nos zumbis do host (2026-11-02) ✅

- O client detecta o próprio golpe (ação da camada 0 de arma branca: `BluntLight_*`, `Strike_*`, `EdgedLight_*`...) ou tiro (`Gun_shoot*`, `Shoot_*` na camada 1) e escolhe o fantoche atingido: golpe = mais próximo na frente, até 2,8 m (±70°); tiro = mais perto da linha da mira (até 60 m, 1 m de tolerância).
- Pacote `Hit` (client → host, 3 cópias, com sequência para descartar repetidos).
- O host aplica o dano: `actor:SetHealth(vida - dano)`; se zerar, `entidade:Kill(true, ...)` (morte normal do jogo, com animação e corpo). `[zombies] melee_damage=35`, `shot_damage=60`.
- Teste: 3 golpes do client numa zumbi comum → 100 → 65 → 30 → morta; golpes extras ignorados ("já morto"); a queda apareceu nas duas janelas (confirmado pelo usuário).

Limitações da etapa 2a:
- O alvo é escolhido pela posição do fantoche no client (sem colisão física); dano fixo por tipo de ataque, sem tiro na cabeça nem diferença entre armas.
- Sem reação ao golpe (cambalear) antes da morte; `NotifyHitReaction` existe na entidade e ainda não foi usado.
- Tiro sem mira (pelo quadril) usa o último alvo do IK de mira.
- Zumbis ainda não atacam o client.
- Um zumbi local do client (`ULZombie1`, provavelmente de missão/cena) reaparece mesmo com `zm_killAll`.

## Zumbis — etapa 2b: ataques ao client (investigação, 2026-11-02)

A IA do jogo não serve para isso (ver o fim da seção); a solução está na etapa 2c. O que foi testado no host (via `sodcoop_cmd.lua`):

| Tentativa | Resultado |
|---|---|
| ProtoAlly criado pelo Lua como "corpo" do client | criado (250 de vida), mas ignorado pelos zumbis |
| NPC da comunidade `ULNPC00` (um dos 12 no mapa) levado até um zumbi | inativo (entidade guardada); ignorado |
| Iscas `Zombie.CreateDrawAggro` / `AggroInCircle` / `ActivateInCircle` | só afetam zumbis "lab" (simulação); o ativo não se moveu |
| `actor:SetEntitySlot(1, alvo)` + `StateMachine=false` + `Pathfinding_AI=true` (árvore `ZombieAttacker`) | variáveis mudam, o zumbi não se move |
| Goal pipe próprio (`+ul_locateentity` + `+stick`) com `SelectPipe`, inclusive com `zm_activationDist 60` | `SelectPipe` devolve true, o zumbi não se move |

Achados úteis: a percepção (`DeterminePerception`, `0x01321D20`) é calculada só para o ator local; o Cry AI vale só a menos de `zm_activationDist` (15 m) do jogador do host (ajustável com `zm_tuneActivationDist 1`); `Zombie.SpawnZombies(pos, n)` cria zumbis (útil para testes). Movimento nativo (descompilação de `UpdateLiveZombies` `0x01346070`, `TryPathZombieTo` `0x0133F930`, `SetZombieObjective` `0x0133D4CB`, `DetermineBehaviors` `0x01321BD0`, `ProcessStateMachineBehavior` `0x0138A220`):
- O `ZombieManager` é orientado a dados: arrays globais por zumbi (posição em `0x01DF2F80`, passo 12; flags em `0x01DF4B80`), 0x18 vagas de zumbi "vivo" em `manager+0x4AE0` e comportamentos por tabela de funções. A posição do jogador vem sempre de `GetClientActor` (o jogador local).
- Os slots do ator (`SetEntitySlot` → `0x0138C900` → `FUN_0158E810`, máquina de estados em `ator+0x1100`) só são gravados pelo bind Lua: o gerenciador não usa slot para o alvo.
- `actor:SetMovementTarget(pos | id)` não move o zumbi; `entity:StartAnimation` no zumbi devolve false.
- **Funciona:** conduzir o zumbi pelo mod (`SetWorldPos` + `SetWorldAngles` a cada frame): ele vai até o fantoche e fica lá.

## Zumbis — etapa 2c: zumbis do host atacam o client (2026-11-02) ✅

Como a IA do jogo só persegue o jogador do host, o mod conduz os zumbis perto do client (função Lua `SodCoop.ZChase`, a cada frame no host):
- Assume o zumbi vivo a menos de `[zombies] aggro_radius` (10 m) do jogador do client e mais perto dele do que do host; solta quando o client passa de 16 m ou o host fica 3 m mais perto.
- Vira para o client e corre (`SetWorldPos`): 3,5 m/s; Feral 6,5; Juggernaut 2,8; Bloater 2,2. A altura acompanha a do client.
- Para a 1,3 m e golpeia (até 1,6 m) a cada 1,8 s, alternando `ZombieMelee_strike_left_fast`, `_strike_right_fast` e `_attack_close` (nomes lidos da memória do host; os paks são cifrados).
- A animação escolhida vai no `ZombieEntry.anim` (o fantoche do client mostra a corrida e o golpe).
- 0,4 s depois do início do golpe, se o client ainda estiver a até 2,2 m, o host manda um `Hit` (kind 2) ao client, que tira `[zombies] attack_damage` (8) da vida do próprio jogador (`actor:SetHealth`) e treme a câmera.
- `[zombies] chase=0` desliga.

Teste (duas instâncias, client a ~32 m do host, Feral levado para 9 m do client): o Feral correu até o client e golpeou; a vida caiu 75 → 67 → 59 → … → 1 e o personagem ficou incapacitado (estado do próprio jogo com pouca vida). Corrida e golpe confirmados pelo usuário na tela do client.

Limitações da etapa 2c:
- A vida do client parava em 1 (resolvido na etapa 2d).
- No host, o zumbi conduzido não mostra o golpe (o jogo recusa animação direta no zumbi): ele aparece parado ao lado do fantoche.
- Perseguição em linha reta, sem caminho (atravessa obstáculos); sem desvio nem bloqueio do client.
- Não testado com zumbis longe do host (fora da IA completa, ~15 m), nem com vários zumbis ao mesmo tempo.
- O Feral usa as animações dos zumbis comuns (pode não ter golpes próprios no fantoche).
- `actor:SetHealth` no client respeita o teto de vida rebaixado por ferimentos (pedido 75, ficou 35).
- `Zombie.SpawnZombies` deixou de criar zumbis nos testes; para testar, zumbis existentes são levados até o client.

## Zumbis — etapa 2d: morte do jogador do client (2026-11-02) ✅

- Golpe de zumbi que zera a vida do client mata pelo caminho normal do jogo: `g_localActor:Kill(true, NULL_ENTITY, NULL_ENTITY, false, false)` (`BasicActor:Kill`, `scripts/entities/actor/basicactor.lua`). Golpes num jogador já morto são ignorados.
- O client manda `State.dead` (vida <= 0, lida por `IActor::GetHealth`; protocolo v13). Com o client morto, o host solta os zumbis conduzidos (`SodCoop.zc = {}`) e para de mandar golpes.
- Na morte, a vida 0 passa pelo `CActor::SetHealth` base (`0x01509140`): vida <= 0 chama a rotina de morte do ator (vtbl `+0x370`) e avisa o jogo.

Testes (duas instâncias, backup dos saves em `testenv/_save_backups/20261102_185308`):
- `Kill` direto no jogador do client: vida 0, `IsDead` = true, entidade continua; o usuário confirmou a morte normal na tela.
- Zumbi comum (ULZombie37) levado a 6 m do client: 35 → 27 → 19 → 12 → 5 → morto; log `jogador local MORREU`; depois disso, nenhum golpe a mais.

Achados:
- `actor:Revive()` num jogador morto **fechou o jogo** (client sumiu sem log de saída); não usar.
- Levar um zumbi para longe do host (2 km) não funciona: o `ZombieManager` o traz de volta para perto do host.
- Logo depois de carregar o save, os zumbis perto do host podem existir só na simulação "lab" (`Zombie.NumZombiesInCircle` conta 31 em 150 m, nenhuma entidade); `Zombie.ActivateInCircle` e `zm_activationDist` maior não criaram entidades. Andar com o host resolveu.
- Para testar com o client longe do host, dá para teleportar o jogador do client (`g_localActor:SetWorldPos`).

Limitações da etapa 2d:
- Com o client morto, o host também deixa de listar zumbis perto do corpo dele (só os perto do host).
- Não há reviver nem troca de personagem sincronizada; depois da morte, o client segue o fluxo normal do jogo sozinho.

## Zumbis — etapa 2e: vários zumbis no client (2026-11-02, parcial)

- `SodCoop.ZChase` passou a separar os zumbis conduzidos: cada um fica a pelo menos 0,9 m dos outros e a pelo menos 1,0 m do client.
- Teste: dois zumbis (ULZombie19 e ULZombie28) golpearam o client em sequência (55 → 47 → 39 → 31). A separação **não foi medida**: nunca houve dois zumbis parados juntos no client por tempo suficiente.

Achados:
- Só zumbis **ativos** (a menos de ~15 m do host) podem ser movidos pelo mod; os mais longe são recolocados pelo `ZombieManager` a cada frame (empurrar 0,3 m a cada 50 ms quase não os moveu). `zm_activationDist` maior não mudou isso.
- Teleporte grande de um zumbi ativo às vezes é desfeito em menos de 0,2 s; aproximar aos poucos (0,2 m a cada 50 ms) funciona.
- Com o host perto do client (~8 m), a IA do jogo puxa os zumbis de volta para o host e ganha do mod; com o host a 20 m ou mais, o mod mantém o zumbi no client.
- Os nomes `ULZombie<n>` **podem repetir** (dois `ULZombie3`, três `ULZombie4` vivos ao mesmo tempo). Corrigido em seguida: o mod passou a usar o `EntityId` (ver abaixo).
- Matar com `ZDamage` um zumbi conduzido no meio de um golpe **fechou o host** uma vez (log `testenv/host/sodcoop_host_crash_zkill.log`); matar um Feral conduzido funcionou antes. Causa não investigada.
- Logo depois de carregar, os zumbis do grupo que o host vê ao longe ainda não são entidades: só viram zumbis ativos quando o host chega a ~15 m.

### Ids de zumbi = EntityId (protocolo v14)

- O host identifica cada zumbi pelo `EntityId` (u32), e não mais pelo número do nome. No Lua, o handle `e.id` só mostra o número pelo `tostring` ("userdata: 0002FCFA"); `SodCoop.EId(e)` converte e `SodCoop.zById` guarda a entidade de cada id.
- `ZombieEntry.id` e `Hit.zombieId` passaram a u32; o fantoche do client, o dano do client (`ZDamage`) e a perseguição (`SodCoop.zc`) usam o mesmo id.
- Os `EntityId` dos zumbis passam de 16 bits (ex.: 0x0002FCFA, 0x0001FD8A), por isso o tipo da rede mudou.
- Teste (duas instâncias): espelho e perseguição com os ids novos; dois zumbis (196344 e 196345) golpearam o client em sequência (25 → 17 → 10 → 3 → 1, vida travada em 1 só no teste). A distância mínima entre eles medida foi 1,96 m (ainda chegando); a separação com os dois parados ao lado do client continua sem medida.
- `Zombie.SpawnZombies` (bind `0x01567F20` → `ZMSpawnZombies` `0x0134E250`, argumentos: origem xyz, destino xyz, quantidade, ?, flags) roda sem erro com várias combinações de argumentos, mas não criou zumbis; causa não investigada.

## Mesmo mundo — etapa 1: mesmo save e nascer ao lado do host (2026-11-02) ✅

- `launch.ps1 -SyncSave`: antes de abrir o client, copia o save do Breakdown (`USER/<steamid>/Sandbox*.ulsave`) do host para o client. O save anterior do client vai para `testenv/_save_backups/<data>_client_antes_sync`. As duas cópias usam a mesma conta Steam (mesma pasta `<steamid>`).
- `[net] spawn_near_host=1` (padrão): o client, ao entrar no mundo a mais de 30 m do jogador do host, é levado para 2 m ao lado dele (`g_localActor:SetWorldPos`), uma vez por entrada no mundo; desiste se o host não aparecer no primeiro minuto.
- Teste: client aberto com o save do host, entrou a 80 m dele e foi levado para o lado (log `spawn: cliente a 80 m do host; levado para o lado dele`); confirmado pelo usuário nas duas janelas.

Limitações da etapa 1:
- O client carrega o último save **gravado** pelo host, não o estado atual da sessão do host.
- Os dois jogam com o **mesmo sobrevivente** (mesmo save); no host, o personagem do client aparece como fantoche, mas a comunidade do client tem o mesmo líder.
- Nada além de jogadores e zumbis é sincronizado: itens, carros, base, comunidade, hora do dia e clima seguem separados em cada cópia.

## Mesmo mundo — etapa 2: hora do dia (2026-11-02) ✅

- O host manda a hora do dia (`State.timeOfDay`, protocolo v15). O client acerta o próprio relógio quando a diferença passa de 0,02 h (~1 min de jogo): `ITimeOfDay::SetTime(hora, true)`. `[net] sync_time_of_day=0` desliga.
- A cvar `e_TimeOfDay` só **mostra** a hora: `System.SetCVar("e_TimeOfDay", h)` não muda nada (o relógio do jogo segue). `sv_timeofdayenable` = 0 no Breakdown.
- Acesso: `gEnv+0x24` (I3DEngine) → vtbl `+0x2E0` `GetTimeOfDay()` → `ITimeOfDay` com `SetTime(float, bool)` em `+0x14` e `GetTime()` em `+0x18` (uso do próprio motor em `0x00794620`).
- Teste: client entrou com 0,98 h de diferença; foi acertado na hora e ficou a ~0,003 h do host (7,217 h × 7,214 h); depois, só um acerto pequeno de vez em quando (o relógio do jogo não desfaz o acerto).
- Clima: não sincronizado. Não foi achado um sistema de clima no Breakdown (só ajustes gráficos de chuva, `r_Rain*`).

## Mesmo mundo — etapa 3: outro sobrevivente e aparência no fantoche (2026-11-02) ✅ parcial

- Troca de sobrevivente: comandos de console do jogo `fm_nextMember` / `fm_prevMember` (registrados em `0x015DF630`). A entidade do jogador continua a mesma ("Dude", id 0x7777); o jogo troca as partes e os dados.
- Um sobrevivente é um esqueleto (`characters/male/human/skeleton/skeleton.chr`) com anexos de pele: `body`, `skin`, `legs`, `head`, `hair`, `pack` (cada um um `.chr`).
- Pacote `Look` (protocolo v16), dos dois lados a cada 2 s: `base=<esqueleto>;anexo=<arquivo.chr>;...`. Quem recebe recria o fantoche só com o esqueleto e prende as partes com a função do próprio jogo que monta os zumbis (`0x0131BCC0`, cdecl `(ICharacterInstance*, nome, arquivo, clonarMaterial)`).
- **Não tirar partes de um personagem em uso**: a primeira versão removia os anexos do fantoche com `DestroyAttachment` e prendia os novos; o host fechou na hora (acesso a nulo em `0x00B2D805`, numa thread de animação; dump `USER/StateOfDecay_LUASPC_2026-11-02_20-18-09.dmp`, log `sodcoop_host_crash_look.log`). Agora uma aparência nova recria o fantoche do zero.
- Teste: os dois fantoches montados com as 6 partes reais, sem falha; `fm_nextMember` no client → o host recriou o fantoche com a nova aparência em < 1 s. Confirmado pelo usuário por imagem (boné, camiseta, mochila e jeans iguais nas duas janelas).

Limitações da etapa 3:
- Cores: resolvidas em seguida (ver "cores das roupas" abaixo).
- Não testado com sobrevivente mulher (outro esqueleto) nem com o client trocando de sobrevivente pelo menu do jogo.
- A troca era manual (`fm_nextMember` pelo `sodcoop_cmd.lua`); agora há teclas (ver abaixo). O NPC do sobrevivente do client continua existindo na comunidade do host.

### Cores das roupas (protocolo v17) ✅

- A cor de cada parte vem de um material de variante (ex.: `characters/male/human/body/tshirt_jeans/variant/v16`, `.../packs/small01/variant/v12`), posto como material substituto do anexo. Lido com `IAttachmentObject::GetMaterial` (`+0x34`) e `IMaterial::GetName` (`+0x18`).
- O `Look` passou a levar `anexo=arquivo.chr|material` (até 1200 bytes). O fantoche carrega o material com `IMaterialManager::LoadMaterial` (`I3DEngine +0x21C` → `+0x14`, **4 argumentos**: nome, criarSeFaltar, bool, bool) e o põe com `SetReplacementMaterial` (`+0x38` e `+0x3C`), logo depois de prender a parte.
- Erro no caminho: com 5 argumentos no `LoadMaterial` (o `-1` empilhado antes é do `CloneMaterial` seguinte), as duas cópias caíram em `EIP=0` ao montar o fantoche (dumps de 20:34:55/56).
- Teste: client trocou de sobrevivente; o host montou o fantoche com as 6 partes e os materiais (camiseta v16, mangas v03, jeans v03, boné v02, mochila v12); o usuário confirmou que as cores batem nas duas janelas.
### Teclas de troca de sobrevivente ✅

- F6 / F7 trocam o jogador local para o sobrevivente anterior/próximo (`fm_prevMember` / `fm_nextMember`), sem precisar de base. Só com a janela daquela cópia em primeiro plano, no mundo e vivo. `[keys] prev_member` / `next_member` (código de tecla virtual do Windows; 0 desliga).
- No jogo, a troca pelo próprio menu só existe numa base, com amigos; o save de teste ainda não tem base. A aparência se atualiza com qualquer troca, pelo pacote `Look`.
- Teste: dois F7 no client; o host recriou o fantoche nas duas vezes. Na segunda, uma **sobrevivente mulher** (esqueleto `characters/female/human/skeleton/...`), com as 6 partes, sem falha. Confirmado pelo usuário.
### Menu de sobreviventes no F7 ✅

- F7 abre/fecha um menu em texto no alto à esquerda; setas escolhem, PgUp/PgDn pulam 10, Enter troca (`fm_nextMember N`, que vai direto ao membro N: `FUN_0148F4C0` → `FUN_014919C0`). F6 continua trocando direto para o anterior.
- Lista de membros: total em `0x01E65864` (149 neste save), registros de 0x34 bytes em `*(0x01E65874)` com o `.cdf` do corpo em +8; membro atual em `*(0x01E658C8)`, índice u16 em +0xF4. O jogo não guarda o nome em texto: cada linha mostra número, sexo e roupa (do caminho do `.cdf`), sem acentos.
- Desenho: a função de texto 2D de depuração do jogo (`0x00473160`, cdecl `(IRenderer* = gEnv+0x10, x, y, tamanho, rgba*, centralizar, formato, ...)`, a mesma do `fm_stats`). `System.DrawText` (Lua) não aparece neste jogo; `System.DrawLabel` (ponto 3D) aparece, mas seguia a câmera com atraso.
- Correção junto: o fantoche passou a ser localizado pelo id guardado no Lua (`puppet::entity()`), não pelo nome. Pelo nome, logo depois de recriá-lo o mod achava o fantoche anterior (que existe até o fim do frame) e a aparência entrava em laço de recriação.
- Teste: menu fixo e legível (confirmado pelo usuário); troca pelo menu para o nº 86 (mulher); o host recriou o fantoche uma única vez, com o esqueleto feminino e as 6 partes.
### Fantoche sem partes (2026-11-03)

- Visto uma vez: no client, o fantoche do host existia no lugar certo, visível, mas com o personagem recarregado (0 anexos: sem partes nem arma) logo depois de montado; só o esqueleto, que não aparece. Causa não achada (na mesma hora o client tinha sido levado de 2 km para o lado do host).
- Proteção: a cada 2 s o mod confere se o fantoche montado ainda tem partes; se não tiver, registra `aparencia: fantoche perdeu as partes; recriando` e recria. No teste seguinte o problema não se repetiu.
## Carros — etapa 1: carro do outro jogador (2026-11-03) ✅

- O carro em que o jogador está vem de `actor:GetLinkedVehicleId()`; a entidade tem `.vehicle` e a classe é o tipo (ex.: `ULVEH00`, classe `Suv_New`). Os carros do mundo **não** são os mesmos nas duas cópias (no client não havia carro naquele ponto), então o outro lado cria o seu.
- `State` leva `vehicle` (classe), `vehiclePos` e `vehicleAngles` (protocolo v18). Quem recebe cria um carro da mesma classe (`System.SpawnEntity`, nome `sodcoop_vehicle`) com a física desligada (`EnablePhysics(false)`: com física ele flutuava/quicava) e o move a cada frame, interpolado 0,1 s atrás. Ao sair, o carro some depois de 0,5 s.
- Motorista: a posição do jogador (que o jogo põe no banco) relativa ao carro, nos eixos do carro, vira o lugar do fantoche; ele gira com o carro e fica na pose `Vehicle_truck_steer` (t 0,5, volante reto), a animação do jogador dirigindo. Ao sair, o fantoche é recriado em pé.
- Correção no caminho: com o host já no carro e o client carregando o mundo, o mod lia o Lua antes de `settled` e o motor avisou "Unremovable entities should never change their name"; agora espera o mundo pronto.
- Teste: host dirigindo um `Suv_New`; no client o carro andou junto e o host apareceu sentado ao volante; ao sair, voltou em pé e o carro sumiu. Confirmado pelo usuário.

Limitações da etapa 1:
- ~~O carro só existe enquanto o outro jogador está dentro~~ (resolvido na etapa 2).
- O carro de verdade do outro lado não se move (se existir lá, fica no lugar).
- Só o motorista: o outro jogador não pode entrar no carro como passageiro; sem portas, luzes, dano nem som.
- O volante fica parado (pose fixa); as rodas não giram.

## Carros — etapa 2: carro estacionado (2026-11-03) ✅

- `State` leva também `vehicleId` (protocolo v19): o `EntityId` do carro no remetente. Quem recebe guarda um carro-fantoche por id (`SodCoop.rvs[id]`, nome `sodcoop_vehicle_<id>`).
- Ao sair (0,5 s a pé), o carro não é removido: `SodCoop.ParkRemoteVehicle` liga a física e ele fica parado, no chão e com colisão. Ao voltar para o mesmo carro, o fantoche é reaproveitado (física desligada de novo); trocar de carro deixa o anterior estacionado.
- Ao desconectar, todos os carros do outro jogador são removidos (`SodCoop.RemoveRemoteVehicles`).
- Teste: host entrou num `Suv_New`, saiu e voltou ao mesmo; no client o carro ficou estacionado e, na volta, só havia um carro (log: "dirigindo o carro 130828 (Suv_New), o mesmo de antes"). Confirmado pelo usuário.

Limitações:
- ~~O carro continua não sendo o mesmo carro do mundo~~: com o save copiado, passou a ser (etapa 3).
- O client não pode dirigir o carro estacionado do host (não testado).

## Carros — etapa 3: passageiro no carro do outro jogador (2026-11-03) ✅

- **Mesmo carro dos dois lados:** com o save copiado (`-SyncSave`), o carro do host também existe no mundo do client, com o mesmo **nome** de entidade (ex.: `ULVEH00`) e a mesma classe, mas com **outro id**. `State.vehicleName` (protocolo v20) leva o nome; quem recebe procura até 30 m um carro com essa classe e esse nome (`SodCoop.FindSameCar`) e o usa (`sodcoopBorrowed`) em vez de criar um. Ao desconectar, o carro usado só volta a ter física (não é removido).
- Logo depois de o client entrar no mundo, o carro igual ainda não aparece em `GetEntitiesInSphere`: o mod cria um, procura de novo a cada ~1 s (até 30 vezes) e troca o criado pelo carro igual quando o encontra.
- Sem isso havia dois carros no mesmo lugar: o client entrava no do mundo dele e o host criava uma cópia. Uma tentativa intermediária (esconder o "gêmeo" num raio de 4 m) falhou: no client o gêmeo estava a 4,1 m, e no host a regra escondeu o próprio carro do host.
- **Entrar:** o client usa a tecla normal de usar. Entrando pela porta do passageiro com o volante vazio (no client, o host não está de verdade no carro), o jogo desliza o jogador para o volante (`Vehicle_truck_scooch_passenger_driver`, ~1,5 s depois). O mod devolve: `vehicle:ChangeSeat(id, 2, false)`. **`ChangeSeat` recebe uma escolha, não o número do assento**: 2 levou do assento 1 (volante) para o 3 (ao lado); `ChangeSeat(id, 3)` não fez nada. Apertar "usar" já como passageiro também desliza para o volante.
- Passageiro no carro do outro: o client vai junto sem nada do mod (o jogo move o passageiro com o carro, mesmo sem física e movido por `SetWorldPos`). `State.passengerOf` = id (no host) do carro e `State.passengerSeat` = assento.
- **Posição do passageiro:** com o jogador sentado, `GetWorldPos` do ator fica no ponto do **motorista** em qualquer assento (o client aparecia em cima do host). Por isso o host não usa a posição do client: o banco do passageiro é o do próprio motorista espelhado no eixo lateral (Suv_New: motorista −0,42; 0,48; 1,0 m nos eixos do carro; passageiro medido logo ao entrar: 0,45; 0,46).
- Pose do fantoche: `Vehicle_pickupold_passenger_idle` (nomes = `Vehicle_` + arquivo em `animations/characters/hero_male/vehicle/`). Ao sair, o fantoche é recriado em pé.
- `Seats[i].seat` tem `SetPassenger`, mas não grava um não-ator (o fantoche); `GetSeatPos` devolve nil. Os scripts de veículo do `gamedata.pak` não abrem (`zipfile` dá erro de dados: pak protegido).
- Teste final (versão instalada, do zero): host no carro antes de o client carregar; o client começou com carro criado, trocou pelo `ULVEH00` dele, entrou pela tecla de usar, foi para o assento 3, andou junto e saiu normal; no host o fantoche ficou sentado ao lado do motorista. Confirmado pelo usuário.

Limitações:
- Bancos de trás (2, 4, 5) não medidos: no host o fantoche vai sempre para o banco da frente.
- Só o client como passageiro do host foi testado (o contrário usa o mesmo código).
- Para os testes, a perseguição de zumbis ao client está desligada no `sodcoop.ini` do host (`[zombies] chase=0`).

## Janelas (2026-11-03)

- O jogo voltava à resolução salva (1920×1080 a partir de 0,0, parecendo tela cheia) e o `window_keeper` o trazia de volta; o client precisava de Alt+Tab. Agora o mod aplica `r_Fullscreen=0` e `r_Width`/`r_Height` = área interna da janela (`[window]` − borda de 16×39 px) ao iniciar. Trocar a resolução com o jogo aberto funcionou sem fechar.
- Ao aplicar a resolução, o jogo centraliza a janela sem mudar o tamanho; o `window_keeper` agora corrige também a posição (antes só o tamanho), para as duas ficarem no topo, lado a lado.

## Itens — etapa 1: lugares vasculhados (2026-11-03) ✅

- Os lugares de vasculhar são nós do mundo RTS, com os mesmos números nas duas cópias (detalhes em `RE_ENDERECOS.md`, 4g). Módulo `src/game/loot.cpp`, a cada 0,5 s.
- **Marca de vasculhado:** pacote `Searched` (protocolo v21), a cada 1 s, com um bit por nó (211 bytes para 1688 nós). Quem recebe faz `flags[nó] |= 1` no que o outro já vasculhou; nunca desmarca. Com a flag ligada, o jogo não sorteia itens de novo ao abrir.
- **O que sobrou dentro:** pacote `Container`, com a entrada do histórico de busca do jogo (nó, itens, quantidades, tipos), sempre que muda aqui e de novo a cada 2 s. Cada nó tem uma versão: quem muda soma 1, e vale a maior; com versões iguais e conteúdos diferentes (os dois começaram do mesmo save), vale o do host. Sem entrada no histórico, o mod cria uma pelo mesmo caminho do jogo (`0x013AD200`) e acerta os recursos (`0x013AD3C0`).
- Teste com as duas cópias:
  - 1ª versão (só a marca): o host vasculhou o nó 500 e pegou arma e molotov. O client recebeu a marca em 1 s; ao abrir, só tinha o lote de comida, que o jogo põe mesmo em lugar marcado.
  - 2ª versão (com o conteúdo): o host vasculhou o nó 506 e pegou um item. O client recebeu as versões 1 e 2 e viu os mesmos itens. O client pegou o último; o host recebeu a versão 3 (vazio) e o lugar deixou de ser interativo, como num lugar vazio. Confirmado pelo usuário.

Limitações:
- O histórico do jogo guarda só os últimos 20 lugares; um lugar que sai do anel perde o que sobrou (igual ao jogo sozinho).
- Pegar ao mesmo tempo o mesmo item nos dois lados duplica o item (vale a última versão que chegar).
- Prédios inteiros marcados como vasculhados (flag do prédio) não são enviados; só os nós.
- Lotes de recurso carregados, estoque da base e itens no chão ficam para as próximas etapas.

## Itens — estoque da base (2026-11-03, investigado)

- Achado na memória (`RE_ENDERECOS.md`, 4h), com a codificação do jogo. As duas cópias deram 0 nos cinco recursos: no Breakdown atual ainda não há base ("Encontrar uma nova base"). Sincronizar fica para quando houver base.

## Itens — etapa 2: dar item ao outro jogador (2026-11-03) ✅

- **F8** (`[keys] give_item`) abre o menu "Dar item ao outro jogador" (`src/game/give_menu.cpp`, no estilo do F7): lista a mochila do sobrevivente atual; setas escolhem, Enter dá.
- O item sai daqui (`ClearSlot`, vtable `+0x14`) e vai pelo pacote `Give` (protocolo v22, 3 cópias, com sequência) com o id e os 6 bytes da vaga. Lá: empilháveis com `+0x0C` (quantidade); armas numa vaga livre (`+0x1C`, `+0x18`) com os mesmos dados (durabilidade/munição). Se não couber, volta para quem deu (`returned`).
- Rótulos: o jogo não guarda o nome dos consumíveis em texto; o menu mostra o nome interno das armas (`mle wpn crowbar`) e, nos outros, o nome do som (`Item: meds (1)`, `Municao: ammo shell (6)`).
- Correção no caminho: a 1ª versão usava `*(0x01E658F0)`; os logs mostravam o item saindo e chegando, mas a tela não mudava. A mochila que a tela mostra é a do registro do membro atual (`*(membro + 0xE8)`). O save do host foi restaurado do backup antes do novo teste.
- Teste com as duas cópias: host deu munição ×6 ao client; client deu o pé de cabra ao host. Os itens saíram de um lado e apareceram no outro. Confirmado pelo usuário.

Limitações:
- Dá a vaga inteira (a pilha toda).
- Itens no chão, mochila de recurso carregada e armário da base: não sincronizados.

## Zumbis — etapa 3: polimento (2026-11-03, parcial)

- **Perseguição religada** no host (`[zombies] chase=1`), depois dos testes de carro e itens.
- **Golpe visível no host: não dá pelo zumbi de verdade.** `StartAnimation` no zumbi do host tocou o golpe duas vezes e depois o deixou invisível para sempre (vivo, `IsHidden()` falso mesmo depois de `Hide(0)`). O `ZombieManager` controla a animação e o desenho dele. A alternativa seria esconder o zumbi no host e mostrar um fantoche, como no client.
- **Host fechando ao matar um zumbi conduzido:** reproduzido 3 vezes (`ZDamage` → `Kill` pelo host, com o zumbi golpeando o client), sem exceção de memória nem `.dmp`. Depois, 4 mortes seguidas iguais funcionaram (vida 65 e 100, com e sem `zm_playerInvisible`); a causa não foi achada. Logs em `testenv/host/sodcoop_host_crash5/6/7_zkill.log`.
- **Registro de fechamento** (`src/core/crash_log.cpp`): filtro de exceção não tratada (encadeado ao do jogo), registro de 1ª chance para erros graves (até 20) e interceptação de `ExitProcess`/`TerminateProcess`, com o chamador e os endereços do exe na pilha. Grava em `FECHAMENTO:`, `EXCECAO (1a chance):` e `SAIDA:` no log do mod.
- **Zumbis perto do client (só a ~15 m do host):** o `ZombieManager` (`0x0133FF30`) monta a cada frame uma lista de até 20 pontos de interesse (`0x01DEE754` quantidade; posição `0x01E1CAA0`, direção `0x01E1CB90`, tipo `0x01DF0958`, índice `0x01DF096C`, flags `0x01DF0980`): tipo 0 jogador, 1 aliado (`ProtoAlly`), 2 carro (lista em `*(0x01E56AD4)`), 3 eco, etc. Em `DeterminePerception` (`0x01321D20`), o zumbi é ativado (`0x0133CF70`) se o ponto mais perto for do tipo 0 ou 1 e estiver a até `zm_activationDist` (float em `0x01908E30`; desativação em `0x01908E34`), **e** se o zumbi perceber esse ponto (visão com raycast) ou já estiver alerta. `src/game/zombie_activation.cpp` põe o client nessa lista (hook em `0x0140DED0`, no retorno `0x01340711`), como tipo 1 ou 0: o ponto entra, mas nenhum zumbi foi ativado (7 a 10 zumbis "lab" a menos de 40 m do client). Fica desligado (`[zombies] client_activation=0`). `Zombie.AggroInCircle`/`ActivateInCircle` na posição do client também não ativaram.
- **Aparência dos zumbis no client:** os comuns usam um modelo simplificado único (homem de camiseta, `low.cdf`), sem as partes e texturas de cada zumbi; mulheres e roupas diferentes aparecem trocadas. Os especiais (feral, juggernaut...) usam o `.cdf` do tipo.

- **Pedido de ativação direto (2ª tentativa):** `0x0133CF70` é cdecl `(u16 zumbi, float prioridade, u8 flags)` e põe o zumbi numa fila de 24 pedidos ordenada por prioridade (`0x01DF2D90` zumbi, `0x01DF2D30` prioridade, `0x01DF2DC0` flags). Os zumbis "lab" têm posição em `0x01DF2F80` (12 bytes) e flags em `0x01DF4B80` (bit 31 = vaga em uso). Pedir a ativação dos zumbis a até 15 m do client **fechou o host as duas vezes**, no primeiro pedido, ao criar o zumbi: um aviso de "zumbi ativado" (`0x0134E690` → `DAT_01df2e8c` vt `+0x50`) chega a `0x0140A340` → `0x01408A78` → `0x014C883D`, que chama um endereço inválido. Marcar antes o estado de percepção (`0x01DEFD58 |= 0x10`, ponto mais perto `0x01DF0358 = 0`) não mudou nada. Logs em `testenv/host/sodcoop_host_crash8/9_ativacao.log`. Desligado (`client_activation=0`).

- **Resolvido (3ª tentativa):** os fechamentos eram **erro do mod**: `0x0133CF70` é `__stdcall` (`RET 0xC`), e chamá-la como `cdecl` desalinhava a pilha (no registro, `EDI` = `0x01340711`, o retorno do hook). Corrigido, o host deixou de fechar, mas os pedidos ainda eram ignorados: a fila (`0x0131DCF0`) só age com flags (`4`, `8`, `0x10`, `0x40` → `0x013194E0`). Com **flags 8** e prioridade 1,0, como os pedidos de `DeterminePerception` (`0x01325925`), funcionou: com o host a 50 m, 5 zumbis a até 10 m do client viraram entidade, o host os enviou e eles atacaram o client. Confirmado pelo usuário. Ligado por padrão (`[zombies] client_activation=1`).
- Antes de cada pedido, o mod grava o estado de percepção como o jogo faz (`0x01DEFD58 |= 0x10`, `0x01DF0358 = 0`). O ponto do client na lista de interesse ficou opcional e desligado (`client_point_type=-1`).

Pendências:
- A fila tem 24 vagas para os dois jogadores: com muitos zumbis perto do client, os do host podem demorar a ativar (não observado).
- Zumbis ativados longe do host ainda seguem a IA do jogo, que só persegue o host; quem os leva ao client é o `ZChase` (até `aggro_radius`, 10 m).
- Golpe visível no host (fantoche no host).

- **Aparência dos zumbis no client (resolvido, protocolo v23):** o host lê as partes de pele e os materiais de cada zumbi (`appearance::describe`, o mesmo do `Look` do jogador; ex.: mulher com `body`, `head`, `hair`... sobre `characters/female/zombie/skeleton/skeleton.chr`), relendo a cada 3 s (0,3 s enquanto o jogo ainda não prendeu as partes). Manda o texto no pacote `ZombieLook` e o hash dele em `ZombieEntry.lookHash`. O client espera a aparência (até 1 s; 3,5 s se o hash já é conhecido), cria o fantoche com o esqueleto do host e prende as partes (`appearance::attachParts`); sem ela, usa o modelo simplificado e recria o fantoche quando a aparência chega ou muda (nunca troca partes de um personagem em uso). No máximo 4 fantoches criados por frame. Confirmado pelo usuário: zumbis iguais nas duas telas (testado com poucos zumbis).
