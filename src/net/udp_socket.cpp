#include "net/udp_socket.h"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <cstdio>

#include "core/log.h"

namespace sodcoop::net {
namespace {

bool ensureWinsock() {
    static bool ok = [] {
        WSADATA data;
        return WSAStartup(MAKEWORD(2, 2), &data) == 0;
    }();
    return ok;
}

}  // namespace

std::string Address::toString() const {
    in_addr a;
    a.s_addr = ipv4;
    char buf[32];
    snprintf(buf, sizeof(buf), "%s:%u", inet_ntoa(a), ntohs(port));
    return buf;
}

bool resolve(const std::string& host, uint16_t port, Address& out) {
    if (!ensureWinsock()) return false;
    addrinfo hints = {};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    addrinfo* res = nullptr;
    if (getaddrinfo(host.c_str(), nullptr, &hints, &res) != 0 || !res) return false;
    out.ipv4 = reinterpret_cast<sockaddr_in*>(res->ai_addr)->sin_addr.s_addr;
    out.port = htons(port);
    freeaddrinfo(res);
    return true;
}

UdpSocket::~UdpSocket() { close(); }

bool UdpSocket::isOpen() const { return socket_ != INVALID_SOCKET; }

bool UdpSocket::open(const std::string& bindHost, uint16_t port) {
    if (!ensureWinsock()) {
        log::error("net: WSAStartup falhou");
        return false;
    }
    close();
    SOCKET s = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s == INVALID_SOCKET) {
        log::error("net: socket() falhou (%d)", WSAGetLastError());
        return false;
    }
    u_long nonBlocking = 1;
    ioctlsocket(s, FIONBIO, &nonBlocking);
    // Sem isto o Windows reporta WSAECONNRESET no recv quando o outro lado fecha.
    BOOL noReset = FALSE;
    DWORD bytes = 0;
    WSAIoctl(s, _WSAIOW(IOC_VENDOR, 12) /* SIO_UDP_CONNRESET */, &noReset, sizeof(noReset), nullptr, 0,
             &bytes, nullptr, nullptr);

    Address bindAddr;
    if (!resolve(bindHost, port, bindAddr)) {
        log::error("net: endereco de bind invalido '%s'", bindHost.c_str());
        closesocket(s);
        return false;
    }
    sockaddr_in sa = {};
    sa.sin_family = AF_INET;
    sa.sin_addr.s_addr = bindAddr.ipv4;
    sa.sin_port = bindAddr.port;
    if (::bind(s, reinterpret_cast<sockaddr*>(&sa), sizeof(sa)) != 0) {
        log::error("net: bind %s:%u falhou (%d)", bindHost.c_str(), port, WSAGetLastError());
        closesocket(s);
        return false;
    }
    socket_ = s;
    return true;
}

void UdpSocket::close() {
    if (socket_ != INVALID_SOCKET) closesocket(socket_);
    socket_ = INVALID_SOCKET;
}

bool UdpSocket::sendTo(const Address& to, const void* data, int size) {
    if (!isOpen()) return false;
    sockaddr_in sa = {};
    sa.sin_family = AF_INET;
    sa.sin_addr.s_addr = to.ipv4;
    sa.sin_port = to.port;
    return ::sendto(socket_, static_cast<const char*>(data), size, 0, reinterpret_cast<sockaddr*>(&sa),
                    sizeof(sa)) == size;
}

int UdpSocket::receive(void* buffer, int capacity, Address& from) {
    if (!isOpen()) return -1;
    sockaddr_in sa = {};
    int len = sizeof(sa);
    int n = ::recvfrom(socket_, static_cast<char*>(buffer), capacity, 0, reinterpret_cast<sockaddr*>(&sa), &len);
    if (n < 0) return WSAGetLastError() == WSAEWOULDBLOCK ? 0 : -1;
    from.ipv4 = sa.sin_addr.s_addr;
    from.port = sa.sin_port;
    return n;
}

}  // namespace sodcoop::net
