# Protocolo de rede (v4)

Histórico: v1 = posição/yaw; v2 = `State.speed` (escolha de animação); v3 = `State.timeMs` vira relógio de alta precisão (`QueryPerformanceCounter`, ms), base da interpolação; v4 = `State.action`/`actionSeq` (ataques, pulos e outras ações). Versões diferentes são recusadas (`Reject(2)`).

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
| `version` | u16 | `kProtocolVersion` = 4; se for diferente, o host responde `Reject(2)` |
| `type` | u8 | 1 Hello, 2 Welcome, 3 Reject, 4 State, 5 Bye |
| `reserved` | u8 | 0 |
| `senderId` | u32 | token aleatório da instância (identificação do jogador) |
| `seq` | u32 | sequência crescente por remetente; `State` com `seq` ≤ último aceito é descartado |

| Tipo | Corpo | Direção |
|---|---|---|
| Hello | `char name[32]` | cliente → host, a cada 500 ms até receber Welcome |
| Welcome | `u32 clientId` | host → cliente |
| Reject | `u8 reason` (1 = sessão cheia, 2 = versão) | host → cliente |
| State | `u32 timeMs, u8 hasPlayer, f32 pos[3], f32 yaw, f32 speed, char action[48], u16 actionSeq` | ambos, a `send_rate_hz` (também serve de keep-alive) |
| Bye | — | ambos, ao encerrar |

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

## Configuração (`sodcoop.ini`, seção `[net]`)

```
role=off|host|client
bind=127.0.0.1      ; host: 0.0.0.0 para LAN (pode exigir liberar no firewall)
host=127.0.0.1      ; cliente: endereço do host
port=27650
send_rate_hz=20
timeout_ms=5000
```
