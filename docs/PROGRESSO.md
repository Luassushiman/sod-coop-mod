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
- Pistola: idle `relaxed_idle_pistol` + `Gun_shoot_revolver` parado em t=0,15 na camada 1 (braço estendido). ⚠️ **Não testado** (o personagem de teste não tinha pistola).

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
