// Sessão de rede do protótipo: 1 host + 1 cliente, UDP.
// Roda numa thread própria (envio a send_rate_hz, recepção contínua); o jogo
// troca dados com ela só por setLocalState/getRemoteState, protegidos por lock.
//
// Fluxo: cliente envia Hello a cada 500 ms até receber Welcome. Conectados,
// os dois enviam State periodicamente (também é o keep-alive). Sem pacotes do
// outro lado por timeout_ms => desconectado (cliente volta a mandar Hello).
#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace sodcoop::net {

enum class Role { Off, Host, Client };

struct SessionConfig {
    Role role = Role::Off;
    std::string bindHost = "127.0.0.1";
    std::string hostAddress = "127.0.0.1";  // só cliente
    uint16_t port = 27650;
    int sendRateHz = 20;
    int timeoutMs = 5000;
    std::string playerName = "player";
};

struct PlayerState {
    bool hasPlayer = false;  // false no menu/carregando
    float pos[3] = {};
    float yaw = 0;
    float speed = 0;  // m/s, horizontal
    char action[48] = {};  // ver StatePacket.action
    uint16_t actionSeq = 0;
    char weapon[128] = {};  // ver StatePacket.weapon
    char base[48] = {};     // ver StatePacket.base
    char upper[48] = {};    // ver StatePacket.upper
    uint16_t upperSeq = 0;
    bool aiming = false;
    float aimOffset[3] = {};  // alvo da mira relativo à posição
    char aimPose[48] = {};    // ver StatePacket.aimPose
};

struct RemoteState {
    bool connected = false;
    uint32_t peerId = 0;
    PlayerState state;
    uint32_t receivedAtMs = 0;    // GetTickCount local da chegada
    double receivedAtSec = 0;     // clock::seconds() local da chegada
    uint32_t senderTimeMs = 0;    // StatePacket.timeMs
    uint32_t sequence = 0;
};

// Zumbi como trafega na rede (host -> cliente).
struct ZombieState {
    uint16_t id = 0;
    bool loop = false;
    float pos[3] = {};
    float yaw = 0;
    char anim[32] = {};
    char model[64] = {};
};

// Último estado recebido de um zumbi, com o anterior para interpolar.
struct RemoteZombie {
    ZombieState cur, prev;
    double curAtSec = 0, prevAtSec = 0;  // clock::seconds() local da chegada
};

// Golpe do cliente num zumbi do host, ou de um zumbi do host no jogador do cliente.
struct HitEvent {
    uint16_t zombieId = 0;
    uint8_t kind = 0;  // 0 = corpo a corpo, 1 = tiro (do cliente); 2 = ataque de zumbi (do host)
    float damage = 0;
};

bool start(const SessionConfig& cfg);
void stop();

void setLocalState(const PlayerState& s);
RemoteState getRemoteState();

// Host: zumbis a enviar na próxima rodada (10 Hz).
void setLocalZombies(const std::vector<ZombieState>& zombies);
// Cliente: zumbis recebidos (id -> estado). Entradas sem atualização somem
// depois de 1,5 s.
std::map<uint16_t, RemoteZombie> getRemoteZombies();

// Envia um golpe ao outro lado (3 cópias; quem recebe descarta as repetidas).
void sendHit(const HitEvent& hit);
// Golpes recebidos desde a última chamada.
std::vector<HitEvent> takeHits();

}  // namespace sodcoop::net
