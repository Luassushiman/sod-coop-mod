// Sessão de rede do protótipo: 1 host + 1 cliente, UDP.
// Roda numa thread própria (envio a send_rate_hz, recepção contínua); o jogo
// troca dados com ela só por setLocalState/getRemoteState, protegidos por lock.
//
// Fluxo: cliente envia Hello a cada 500 ms até receber Welcome. Conectados,
// os dois enviam State periodicamente (também é o keep-alive). Sem pacotes do
// outro lado por timeout_ms => desconectado (cliente volta a mandar Hello).
#pragma once

#include <cstdint>
#include <string>

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

bool start(const SessionConfig& cfg);
void stop();

void setLocalState(const PlayerState& s);
RemoteState getRemoteState();

}  // namespace sodcoop::net
