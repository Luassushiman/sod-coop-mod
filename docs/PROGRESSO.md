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
