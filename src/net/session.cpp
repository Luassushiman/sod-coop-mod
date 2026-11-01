#include "net/session.h"

#include <windows.h>
#include <mmsystem.h>
#include <cstring>

#include "core/clock.h"
#include "core/log.h"
#include "net/protocol.h"
#include "net/udp_socket.h"

static_assert(sizeof(sodcoop::net::PlayerState::action) == sodcoop::net::kActionNameSize);
static_assert(sizeof(sodcoop::net::PlayerState::weapon) == sodcoop::net::kWeaponPathSize);
static_assert(sizeof(sodcoop::net::PlayerState::base) == sodcoop::net::kActionNameSize);
static_assert(sizeof(sodcoop::net::PlayerState::upper) == sodcoop::net::kActionNameSize);
static_assert(sizeof(sodcoop::net::PlayerState::aimPose) == sodcoop::net::kActionNameSize);

namespace sodcoop::net {
namespace {

SessionConfig g_cfg;
UdpSocket g_socket;
SRWLOCK g_lock = SRWLOCK_INIT;
PlayerState g_local;       // protegido por g_lock
RemoteState g_remote;      // protegido por g_lock
volatile bool g_running = false;
HANDLE g_thread = nullptr;

uint32_t g_myId = 0;
uint32_t g_sendSeq = 0;
Address g_peerAddr;        // endereço do outro lado (cliente: o host; host: o cliente aceito)
bool g_peerKnown = false;  // host: tem cliente; cliente: recebeu Welcome
uint32_t g_lastRecvMs = 0;
uint32_t g_lastPeerSeq = 0;

const char* roleName(Role r) { return r == Role::Host ? "host" : r == Role::Client ? "client" : "off"; }

void fillHeader(Header& h, PacketType t) {
    h.magic = kMagic;
    h.version = kProtocolVersion;
    h.type = t;
    h.reserved = 0;
    h.senderId = g_myId;
    h.seq = ++g_sendSeq;
}

void send(const void* pkt, int size) {
    if (g_peerAddr.valid()) g_socket.sendTo(g_peerAddr, pkt, size);
}

void sendHello() {
    HelloPacket p = {};
    fillHeader(p.h, PacketType::Hello);
    strncpy(p.name, g_cfg.playerName.c_str(), sizeof(p.name) - 1);
    send(&p, sizeof(p));
}

void sendState() {
    StatePacket p = {};
    fillHeader(p.h, PacketType::State);
    p.timeMs = static_cast<uint32_t>(static_cast<uint64_t>(clock::seconds() * 1000.0));
    AcquireSRWLockShared(&g_lock);
    p.hasPlayer = g_local.hasPlayer ? 1 : 0;
    memcpy(p.pos, g_local.pos, sizeof(p.pos));
    p.yaw = g_local.yaw;
    p.speed = g_local.speed;
    memcpy(p.action, g_local.action, sizeof(p.action));
    p.action[sizeof(p.action) - 1] = 0;
    p.actionSeq = g_local.actionSeq;
    memcpy(p.weapon, g_local.weapon, sizeof(p.weapon));
    p.weapon[sizeof(p.weapon) - 1] = 0;
    memcpy(p.base, g_local.base, sizeof(p.base));
    p.base[sizeof(p.base) - 1] = 0;
    memcpy(p.upper, g_local.upper, sizeof(p.upper));
    p.upper[sizeof(p.upper) - 1] = 0;
    p.upperSeq = g_local.upperSeq;
    p.aiming = g_local.aiming ? 1 : 0;
    memcpy(p.aimOffset, g_local.aimOffset, sizeof(p.aimOffset));
    memcpy(p.aimPose, g_local.aimPose, sizeof(p.aimPose));
    p.aimPose[sizeof(p.aimPose) - 1] = 0;
    ReleaseSRWLockShared(&g_lock);
    send(&p, sizeof(p));
}

void setConnected(bool connected, uint32_t peerId) {
    AcquireSRWLockExclusive(&g_lock);
    g_remote.connected = connected;
    g_remote.peerId = peerId;
    if (!connected) g_remote.state = PlayerState{};
    ReleaseSRWLockExclusive(&g_lock);
}

void disconnect(const char* reason) {
    log::warn("net: desconectado de %s (%s)", g_peerAddr.toString().c_str(), reason);
    g_peerKnown = false;
    g_lastPeerSeq = 0;
    if (g_cfg.role == Role::Host) g_peerAddr = Address{};
    setConnected(false, 0);
}

void handlePacket(const char* data, int size, const Address& from) {
    if (size < (int)sizeof(Header)) return;
    Header h;
    memcpy(&h, data, sizeof(h));
    if (h.magic != kMagic || h.senderId == g_myId) return;
    if (h.version != kProtocolVersion) {
        log::warn("net: pacote de %s com versao %u (esperada %u) ignorado", from.toString().c_str(),
                  h.version, kProtocolVersion);
        if (g_cfg.role == Role::Host && h.type == PacketType::Hello) {
            RejectPacket r = {};
            fillHeader(r.h, PacketType::Reject);
            r.reason = 2;
            g_socket.sendTo(from, &r, sizeof(r));
        }
        return;
    }

    if (g_cfg.role == Role::Host && h.type == PacketType::Hello && size >= (int)sizeof(HelloPacket)) {
        if (g_peerKnown && !(from == g_peerAddr)) {
            RejectPacket r = {};
            fillHeader(r.h, PacketType::Reject);
            r.reason = 1;
            g_socket.sendTo(from, &r, sizeof(r));
            log::warn("net: Hello de %s recusado (sessao cheia)", from.toString().c_str());
            return;
        }
        HelloPacket hello;
        memcpy(&hello, data, sizeof(hello));
        hello.name[sizeof(hello.name) - 1] = 0;
        if (!g_peerKnown) log::info("net: cliente '%s' (id %08X) entrou de %s", hello.name, h.senderId,
                                    from.toString().c_str());
        g_peerAddr = from;
        g_peerKnown = true;
        g_lastRecvMs = GetTickCount();
        setConnected(true, h.senderId);
        WelcomePacket w = {};
        fillHeader(w.h, PacketType::Welcome);
        w.clientId = h.senderId;
        send(&w, sizeof(w));
        return;
    }

    if (g_cfg.role == Role::Client && h.type == PacketType::Welcome) {
        if (!g_peerKnown) log::info("net: conectado ao host %s (id %08X)", from.toString().c_str(), h.senderId);
        g_peerKnown = true;
        g_lastRecvMs = GetTickCount();
        setConnected(true, h.senderId);
        return;
    }
    if (g_cfg.role == Role::Client && h.type == PacketType::Reject && size >= (int)sizeof(RejectPacket)) {
        log::warn("net: host recusou a conexao (motivo %u)", reinterpret_cast<const RejectPacket*>(data)->reason);
        return;
    }

    // Daqui em diante, só pacotes do par conectado.
    if (!g_peerKnown || !(from == g_peerAddr)) return;
    g_lastRecvMs = GetTickCount();

    if (h.type == PacketType::Bye) {
        disconnect("o outro lado saiu");
        return;
    }
    if (h.type == PacketType::State && size >= (int)sizeof(StatePacket)) {
        if (h.seq <= g_lastPeerSeq) return;  // atrasado ou duplicado
        g_lastPeerSeq = h.seq;
        StatePacket p;
        memcpy(&p, data, sizeof(p));
        AcquireSRWLockExclusive(&g_lock);
        g_remote.state.hasPlayer = p.hasPlayer != 0;
        memcpy(g_remote.state.pos, p.pos, sizeof(p.pos));
        g_remote.state.yaw = p.yaw;
        g_remote.state.speed = p.speed;
        memcpy(g_remote.state.action, p.action, sizeof(g_remote.state.action));
        g_remote.state.action[sizeof(g_remote.state.action) - 1] = 0;
        g_remote.state.actionSeq = p.actionSeq;
        memcpy(g_remote.state.weapon, p.weapon, sizeof(g_remote.state.weapon));
        g_remote.state.weapon[sizeof(g_remote.state.weapon) - 1] = 0;
        memcpy(g_remote.state.base, p.base, sizeof(g_remote.state.base));
        g_remote.state.base[sizeof(g_remote.state.base) - 1] = 0;
        memcpy(g_remote.state.upper, p.upper, sizeof(g_remote.state.upper));
        g_remote.state.upper[sizeof(g_remote.state.upper) - 1] = 0;
        g_remote.state.upperSeq = p.upperSeq;
        g_remote.state.aiming = p.aiming != 0;
        memcpy(g_remote.state.aimOffset, p.aimOffset, sizeof(p.aimOffset));
        memcpy(g_remote.state.aimPose, p.aimPose, sizeof(g_remote.state.aimPose));
        g_remote.state.aimPose[sizeof(g_remote.state.aimPose) - 1] = 0;
        g_remote.receivedAtMs = g_lastRecvMs;
        g_remote.receivedAtSec = clock::seconds();
        g_remote.senderTimeMs = p.timeMs;
        g_remote.sequence = h.seq;
        ReleaseSRWLockExclusive(&g_lock);
    }
}

DWORD WINAPI netThread(LPVOID) {
    // Envio agendado no relógio preciso (próximo envio = anterior + intervalo):
    // com "agora - último >= intervalo" e Sleep de ~15,6 ms a taxa real caía para ~16 Hz.
    const double sendInterval = 1.0 / (g_cfg.sendRateHz > 0 ? g_cfg.sendRateHz : 20);
    double nextSend = 0;
    uint32_t lastHello = 0, lastStats = GetTickCount();
    uint32_t received = 0;
    char buf[kMaxPacketSize];
    while (g_running) {
        Address from;
        int n;
        while ((n = g_socket.receive(buf, sizeof(buf), from)) > 0) {
            ++received;
            handlePacket(buf, n, from);
        }
        const uint32_t now = GetTickCount();
        if (g_peerKnown && now - g_lastRecvMs > (uint32_t)g_cfg.timeoutMs) disconnect("timeout");
        if (g_cfg.role == Role::Client && !g_peerKnown && now - lastHello >= 500) {
            lastHello = now;
            sendHello();
        }
        const double nowSec = clock::seconds();
        if (g_peerKnown && nowSec >= nextSend) {
            sendState();
            nextSend = (nowSec - nextSend > sendInterval) ? nowSec + sendInterval : nextSend + sendInterval;
        }
        if (now - lastStats >= 10000) {
            log::info("net: %s conectado=%d pacotes recebidos(10s)=%u enviados(seq)=%u", roleName(g_cfg.role),
                      g_peerKnown, received, g_sendSeq);
            lastStats = now;
            received = 0;
        }
        Sleep(2);  // com timeBeginPeriod(1), dorme ~2 ms de verdade
    }
    if (g_peerKnown) {
        Header bye;
        fillHeader(bye, PacketType::Bye);
        send(&bye, sizeof(bye));
    }
    return 0;
}

}  // namespace

bool start(const SessionConfig& cfg) {
    g_cfg = cfg;
    if (cfg.role == Role::Off) {
        log::info("net: desligado (role=off)");
        return true;
    }
    LARGE_INTEGER t;
    QueryPerformanceCounter(&t);
    g_myId = (uint32_t)(t.QuadPart ^ (GetCurrentProcessId() << 16)) | 1;

    if (cfg.role == Role::Host) {
        if (!g_socket.open(cfg.bindHost, cfg.port)) return false;
        log::info("net: host escutando em %s:%u (id %08X)", cfg.bindHost.c_str(), cfg.port, g_myId);
    } else {
        if (!g_socket.open(cfg.bindHost, 0)) return false;
        if (!resolve(cfg.hostAddress, cfg.port, g_peerAddr)) {
            log::error("net: nao foi possivel resolver o host '%s'", cfg.hostAddress.c_str());
            return false;
        }
        log::info("net: cliente (id %08X) vai conectar em %s", g_myId, g_peerAddr.toString().c_str());
    }
    timeBeginPeriod(1);  // resolução de 1 ms para o Sleep da thread de rede
    g_running = true;
    g_thread = CreateThread(nullptr, 0, &netThread, nullptr, 0, nullptr);
    return g_thread != nullptr;
}

void stop() {
    if (!g_running) return;
    g_running = false;
    if (g_thread) {
        WaitForSingleObject(g_thread, 1000);
        CloseHandle(g_thread);
        g_thread = nullptr;
    }
    g_socket.close();
    timeEndPeriod(1);
}

void setLocalState(const PlayerState& s) {
    AcquireSRWLockExclusive(&g_lock);
    g_local = s;
    ReleaseSRWLockExclusive(&g_lock);
}

RemoteState getRemoteState() {
    AcquireSRWLockShared(&g_lock);
    RemoteState r = g_remote;
    ReleaseSRWLockShared(&g_lock);
    return r;
}

}  // namespace sodcoop::net
