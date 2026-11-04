// Abre a porta UDP do host no roteador por UPnP (o mesmo recurso que muitos jogos usam), pelo
// componente que já vem no Windows (natupnp.dll, COM). Roda numa thread própria: a busca do
// roteador pode levar alguns segundos. Ao fechar o jogo, tira o encaminhamento.
#pragma once

#include <cstdint>
#include <string>

namespace sodcoop::net::upnp {

// Começa a abrir a porta (assíncrono). O resultado vai para o log e para status().
void openPort(uint16_t port);
void closePort();

struct Status {
    bool done = false;        // terminou (com ou sem sucesso)
    bool mapped = false;      // o roteador aceitou o encaminhamento
    std::string localIp;      // IP deste PC na rede local
    std::string externalIp;   // IP público informado pelo roteador
    bool cgnat = false;       // IP "público" do roteador é privado/CGNAT: de fora não chega
    std::string message;      // resumo para o log e para o programa de configuração
};
Status status();
// Mesma coisa, esperando o resultado (programa de configuração: teste da porta).
Status openPortNow(uint16_t port);

}  // namespace sodcoop::net::upnp
