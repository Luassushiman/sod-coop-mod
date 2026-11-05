# Protocolo de rede (v26)

Histórico: v1 = posição/yaw; v2 = `State.speed` (escolha de animação); v3 = `State.timeMs` vira relógio de alta precisão (`QueryPerformanceCounter`, ms), base da interpolação; v4 = `State.action`/`actionSeq` (ataques, pulos e outras ações); v5 = `State.weapon` (modelo da arma na mão); v6 = `base`/`upper`/`upperSeq`; v7 = `aiming`/`aimOffset`; v8 = `aimPose`; v9 = pacote `Zombies`; v10 = `ZombieEntry.model`; v11 = pacote `Hit`; v12 = `Hit` também host → cliente (ataque de zumbi); v13 = `State.dead`; v14 = ids de zumbi passam a ser o `EntityId` (u32); v15 = `State.timeOfDay`; v16 = pacote `Look`; v17 = `Look` com materiais (1200 bytes); v18 = `State.vehicle`/`vehiclePos`/`vehicleAngles`; v19 = `State.vehicleId`; v20 = `State.vehicleName`/`passengerOf`/`passengerSeat`; v21 = pacotes `Searched` e `Container`; v22 = pacote `Give`; v23 = `ZombieEntry.lookHash` e pacote `ZombieLook`; v24 = pacotes `Locker` e `Resources`; v25 = pacote `Facilities`; v26 = pacotes `SaveReq` e `SaveChunk` (save do host para o cliente). Versões diferentes são recusadas (`Reject(2)`).

Código: `src/net/protocol.h` (formato), `src/net/session.cpp` (lógica) e `src/net/udp_socket.cpp` (transporte).

## Transporte

- UDP/IPv4 por Winsock, com socket não bloqueante e numa thread de rede própria (independente do frame do jogo).
- `SIO_UDP_CONNRESET` desligado, para o `recvfrom` não falhar quando o outro lado fecha.
- A camada de transporte fica isolada (`UdpSocket`) para ser trocada por Steam relay / GameNetworkingSockets ou ENet.

## Pacotes

Todo pacote começa com `Header` (16 bytes, little-endian, `#pragma pack(1)`):

| Campo | Tipo | Descrição |
|---|---|---|
| `magic` | u32 | `0x43444F53` ("SODC"); pacotes com outro valor são ignorados |
| `version` | u16 | `kProtocolVersion` = 26; se for diferente, o host responde `Reject(2)` |
| `type` | u8 | 1 Hello, 2 Welcome, 3 Reject, 4 State, 5 Bye, 6 Zombies, 7 Hit, 8 Look, 9 Searched, 10 Container, 11 Give, 12 ZombieLook, 13 Locker, 14 Resources, 15 Facilities, 16 SaveReq, 17 SaveChunk |
| `reserved` | u8 | 0 |
| `senderId` | u32 | token aleatório da instância (identificação do jogador) |
| `seq` | u32 | sequência crescente por remetente; `State` com `seq` ≤ último aceito é descartado |

| Tipo | Corpo | Direção |
|---|---|---|
| Hello | `char name[32]` | cliente → host, a cada 500 ms até receber Welcome |
| Welcome | `u32 clientId` | host → cliente |
| Reject | `u8 reason` (1 = sessão cheia, 2 = versão) | host → cliente |
| State | `u32 timeMs, u8 hasPlayer, f32 pos[3], f32 yaw, f32 speed, char action[48], u16 actionSeq, char weapon[128], char base[48], char upper[48], u16 upperSeq, u8 aiming, f32 aimOffset[3], char aimPose[48], u8 dead, f32 timeOfDay, char vehicle[32], f32 vehiclePos[3], f32 vehicleAngles[3], u32 vehicleId, char vehicleName[32], u32 passengerOf, u8 passengerSeat` (vehicleId: id do carro no remetente, um carro por id, que fica estacionado quando o jogador sai; vehicleName: nome da entidade, para achar o mesmo carro no outro mundo; passengerOf: id, no outro lado, do carro em que o remetente é passageiro, 0 = não) | ambos, a `send_rate_hz` (também serve de keep-alive) |
| Bye | — | ambos, ao encerrar |
| Zombies | `u32 timeMs, u8 count, ZombieEntry e[count]` (até 10; só as entradas usadas vão na rede). `ZombieEntry` = `u32 id (EntityId no host), u8 flags (bit 0 = loop, bit 1 = morto), f32 pos[3], f32 yaw, char anim[32], char model[64], u32 lookHash` (hash FNV-1a da aparência; 0 = ainda sem partes) | host → cliente, 10 Hz (vários pacotes por rodada) |
| Searched | `u16 nodeCount, u8 bits[512]` (bit n = nó n vasculhado) | ambos, a cada 1 s; quem recebe marca o que o outro vasculhou (só com o mesmo `nodeCount`) |
| Container | `i32 node, u32 version, u32 count, u32 ids[16], u8 counts[16], u8 types[16]` (o que sobrou num lugar vasculhado) | ambos, ao mudar e a cada 2 s; vale a maior `version` (empate: o do host) |
| ZombieLook | `u32 zombieId, u32 lookHash, char parts[até 1200]` (como `Look`: `base=esqueleto.chr;anexo=arquivo.chr|material;...`; vai só até o 0 final) | host → cliente, ao aparecer o zumbi e a cada 3 s (até 4 por rodada) |
| Locker | `u8 count, LockerEntry e[count]` (até 100; `LockerEntry` = `u16 slot, u32 id, u8 data[6]`; id 0 = vaga vazia) | ambos: vagas do armário da base que mudaram (3 cópias); host: todas a cada 5 s |
| Resources | `f32 values[5]` (os 5 recursos do estoque, na ordem do jogo) | ambos: ao mudar (3 cópias); host a cada 5 s |
| Facilities | `u32 facility[32]`, `u32 variant[32]` (hash do FacilityId e do VariantId de cada vaga da base; 0 = vazia) | ambos: ao mudar (3 cópias); host a cada 5 s |
| SaveReq | `u32 transferId, u8 have[128]` (transferId do último `SaveChunk` recebido, 0 = começando; bit n de `have` = pedaço n já recebido) | cliente → host, a cada 250 ms enquanto o save não chegou |
| SaveChunk | `u32 transferId, u32 totalSize, u32 crc, u16 index, u16 count, u16 len, u8 data[até 1100]` (pedaço do pacote do save; `count` 0 = o host não tem save do Breakdown) | host → cliente, até 64 pedaços que faltam por `SaveReq` |
| Give | `u16 giveSeq, u32 itemId, u8 data[6], u8 returned` (item dado ao outro jogador; data = dados da vaga) | ambos, 3 cópias; quem recebe descarta `giveSeq` repetido; se não couber, devolve com `returned = 1` |
| Look | `u16 lookSeq, char parts[1200]` = `base=<esqueleto>;<anexo>=<arquivo.chr>|<material>;...` (partes de pele do slot 0) | ambos, a cada 2 s; quem recebe recria o fantoche quando a aparência muda |
| Hit | `u16 hitSeq, u32 zombieId, u8 kind, f32 damage`. kind 0 = corpo a corpo, 1 = tiro (cliente → host: o cliente acertou o zumbi `zombieId`); 2 = ataque de zumbi (host → cliente: o zumbi `zombieId` acertou o jogador do cliente) | ambos, 3 cópias por golpe; quem recebe descarta `hitSeq` repetido (a sequência recomeça ao reconectar) |

Regras (seguindo o Co-op Anything): nenhum ponteiro ou endereço no protocolo, só valores e IDs; movimento vai em pacotes não confiáveis; desconexão é detectada por timeout de keep-alive (`timeout_ms`), sem confiar só no `Bye`.

## Estados

- **Host:** escuta em `bind:port`. O primeiro Hello válido define o par; Hello de outro endereço com o par ativo recebe `Reject(1)`. Se o par fica sem pacotes por `timeout_ms`, ele é liberado.
- **Cliente:** envia Hello até receber Welcome; conectado, envia State. Em timeout, volta a enviar Hello (reconexão automática).
- `hasPlayer = 0` enquanto o jogador está no menu/carregando ou nos 3 s após entrar no mundo; com isso o outro lado remove o fantoche.

## Envio e reconstrução

- **Envio:** agendado no relógio preciso (`próximo = anterior + 1/send_rate_hz`), com `timeBeginPeriod(1)` na thread de rede; 20 Hz reais medidos.
- **`speed`:** velocidade horizontal do remetente, medida por frame com `QueryPerformanceCounter` e suavizada (τ = 0,35 s).
- **Reconstrução (`src/game/remote_player.cpp`):**
  - buffer de 32 snapshots ordenado por `timeMs`;
  - offset de relógio estimado como o mínimo de `chegada − timeMs`, que desce rápido e sobe devagar;
  - o fantoche é desenhado em `agora − offset − 100 ms`, interpolando posição, yaw (pelo menor ângulo) e speed;
  - além do último snapshot, extrapola no máximo 150 ms;
  - salto > 10 m entre snapshots = teleporte (não interpola).
- **Animação:**
  - passo escolhido por `speed`, com histerese e tempo mínimo de 0,4 s por passo;
  - ritmo = `speed` / velocidade nominal do clipe.
- **Ações (`action`, `actionSeq`):**
  - `action` = nome da animação de ação do remetente (vazio = só locomoção); `actionSeq` muda a cada ação nova, inclusive a mesma repetida;
  - como evento discreto, não é interpolado: vale o do snapshot anterior ao instante desenhado;
  - o receptor só aceita nomes com `[A-Za-z0-9_]` e toca a animação uma vez; com `action` vazio, volta ao passo.
- **Postura (`base`):** animação da camada 0 que substitui o idle/andar (idle com arma, `_AIM_*`); vazio = padrão.
- **Parte de cima (`upper`, `upperSeq`):** animação da camada 1 do remetente (segurar arma, tiro, recarga), tocada na camada 2 do fantoche; idles em loop, o resto uma vez; vazio = para a camada.
- **Mira (`aiming`, `aimOffset`, `aimPose`):** estado do IK de mira do remetente (lido do PoseBlenderAim), alvo relativo à posição e nome da aim pose da arma. Com arma longa, o fantoche usa um clipe com a arma no ombro (parado ou andando/correndo para frente).
- **Arma (`weapon`):** caminho do `.cgf` da arma na mão (vazio = mão vazia), lido a cada 0,5 s; o receptor aplica o estado mais recente (sem o atraso de interpolação) e só aceita `[A-Za-z0-9_/.-]` sem `..`.

## Save do host para o cliente

- **Para quê:** os dois precisam carregar o mesmo save. Antes, o host mandava o arquivo ao amigo e ele copiava à mão.
- **Pacote (`src/net/save_transfer.cpp`):** `Sandbox*.ulsave` e `UserStats-dlc.cfg` (estatísticas do Breakdown) da pasta `<jogo>\USER\<SteamID64>\`, em ordem alfabética: `"SODS"`, `u32` quantidade e, por arquivo, `u16` tamanho do nome, nome, `u32` tamanho e os bytes. Cerca de 230 KB, 211 pedaços.
- **Envio:** o cliente com `[net] sync_save=1` (padrão) pede o save assim que conecta, ainda no menu. O host monta o pacote (de novo quando o cliente recomeça e o último tem mais de 5 s, ou pede um `transferId` que não é o atual) e responde com os pedaços que faltam. Em 127.0.0.1, o save inteiro chegou em 0,75 s.
- **Gravação (cliente):** confere o CRC-32 do pacote; se for igual ao do save daqui, não grava nada. Senão, copia o save atual para `<jogo>\sodcoop_save_backups\<data_hora>\` (guarda os 20 mais novos), grava cada arquivo num temporário e troca com `MoveFileEx`, e apaga os `Sandbox*.ulsave` que o host não tem. Só aceita esses nomes de arquivo, sem caminho.
- **Quando o jogo lê o save (`src/game/save_gate.cpp`):** hooks em `NtCreateFile`/`NtOpenFile` do ntdll mostraram que o jogo só abre `UserStats-dlc.cfg` e `Sandbox.0.ulsave` ao clicar em **Continuar**, nunca antes do menu. Se o clique vier com o envio em andamento, a abertura espera até o save chegar (no máximo 20 s); sem o host conectado, não espera (esperar congelava a tela por 20 s). Depois que o jogo lê o save, ou que o jogador local entra no mundo, o mod não grava mais nada naquela sessão.
- **Na tela:** no menu, o cliente vê no canto superior se está esperando, recebendo (%), se já pode clicar em Continuar ou se falhou (texto 2D do jogo, `0x00473160`, como em `member_menu.cpp`), em português ou inglês conforme `[launcher] language` (sem a chave, o idioma do Windows).

## Zumbis

- O host lista os `ULZombie<n>` num raio de `[zombies] radius` (80 m) de cada jogador; `id` = `EntityId` do zumbi (o número do nome `<n>` pode repetir).
- O cliente guarda o estado atual e o anterior de cada zumbi e interpola entre eles (atraso de ~0,1 s); zumbi sem atualização por 1,5 s é removido.
- O buffer de recepção passou a 1400 bytes (`kMaxPacketSize`).

## Configuração (`sodcoop.ini`, seção `[net]`)

```
role=off|host|client
bind=127.0.0.1      ; host: 0.0.0.0 para LAN (pode exigir liberar no firewall)
host=127.0.0.1      ; cliente: endereço do host
port=27650
send_rate_hz=20
timeout_ms=5000
sync_save=1         ; cliente: recebe o save do host ao conectar (0 = desliga)
```
