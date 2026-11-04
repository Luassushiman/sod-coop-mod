// Transporte pela rede P2P da Steam (ISteamNetworking005 da steam_api.dll do próprio jogo):
// os pacotes vão para o SteamID do amigo, e a Steam faz a ponte (NAT ou relay) sem porta
// aberta. Mesma forma do UdpSocket: um único par, representado por kPeerAddress.
#pragma once

#include <cstdint>
#include <string>

#include "net/udp_socket.h"

namespace sodcoop::net {

class SteamP2P {
public:
    // peer = SteamID64 do outro jogador. Falha se a steam_api.dll não estiver carregada.
    bool open(uint64_t peer);
    void close();
    bool sendTo(const Address& to, const void* data, int size);
    // Bytes lidos (0 = nada); só pacotes do par. from = kPeerAddress.
    int receive(void* buffer, int capacity, Address& from);
    // SteamID64 de quem está jogando nesta cópia (0 se não deu para ler).
    static uint64_t localSteamId();
    static constexpr Address kPeerAddress{0x01000000u, 1};  // "1.0.0.0:1": o amigo da Steam

private:
    void* net_ = nullptr;
    uint64_t peer_ = 0;
    uint32_t lastAcceptMs_ = 0;
};

}  // namespace sodcoop::net
