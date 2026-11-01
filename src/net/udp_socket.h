// Socket UDP não bloqueante (Winsock). Camada de transporte isolada para
// poder ser trocada depois (Steam relay, ENet).
#pragma once

#include <cstdint>
#include <string>

namespace sodcoop::net {

struct Address {
    uint32_t ipv4 = 0;  // ordem de rede
    uint16_t port = 0;  // ordem de rede
    bool operator==(const Address& o) const { return ipv4 == o.ipv4 && port == o.port; }
    bool valid() const { return ipv4 != 0 && port != 0; }
    std::string toString() const;
};

// Resolve "127.0.0.1" / "nome" + porta.
bool resolve(const std::string& host, uint16_t port, Address& out);

class UdpSocket {
public:
    ~UdpSocket();
    // Abre e faz bind (porta 0 = efêmera).
    bool open(const std::string& bindHost, uint16_t port);
    void close();
    bool sendTo(const Address& to, const void* data, int size);
    // Retorna bytes lidos, 0 se não há nada, -1 em erro.
    int receive(void* buffer, int capacity, Address& from);
    bool isOpen() const;

private:
    uintptr_t socket_ = ~uintptr_t(0);
};

}  // namespace sodcoop::net
