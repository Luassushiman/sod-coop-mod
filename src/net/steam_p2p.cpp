#include "net/steam_p2p.h"

#include <windows.h>

#include "core/log.h"

namespace sodcoop::net {
namespace {

// ISteamNetworking005 (Steamworks SDK da época do jogo), vtable em ordem:
// 0 SendP2PPacket(CSteamID, const void*, uint32, EP2PSend, int canal)
// 1 IsP2PPacketAvailable(uint32* tamanho, int canal)
// 2 ReadP2PPacket(void*, uint32, uint32* tamanho, CSteamID* remetente, int canal)
// 3 AcceptP2PSessionWithUser(CSteamID)
// 7 AllowP2PPacketRelay(bool)
// CSteamID por valor = 8 bytes na pilha (como um uint64); o ABI é thiscall do MSVC.
using Send_t = bool(__thiscall*)(void*, uint64_t, const void*, uint32_t, int, int);
using Available_t = bool(__thiscall*)(void*, uint32_t*, int);
using Read_t = bool(__thiscall*)(void*, void*, uint32_t, uint32_t*, uint64_t*, int);
using Accept_t = bool(__thiscall*)(void*, uint64_t);
using AllowRelay_t = bool(__thiscall*)(void*, bool);
// ISteamUser017: 2 GetSteamID() -> CSteamID (classe: volta por ponteiro escondido).
using GetSteamID_t = uint64_t*(__thiscall*)(void*, uint64_t*);

constexpr int kChannel = 0;
constexpr int kSendUnreliable = 0, kSendReliable = 2;
constexpr uint32_t kUnreliableMax = 1200;  // limite da Steam para envio não confiável

template <typename T>
T vfn(void* obj, int index) {
    return reinterpret_cast<T>((*reinterpret_cast<void***>(obj))[index]);
}

void* steamInterface(const char* exportName) {
    HMODULE api = GetModuleHandleW(L"steam_api.dll");
    if (!api) return nullptr;
    using Get_t = void*(__cdecl*)();
    auto get = reinterpret_cast<Get_t>(GetProcAddress(api, exportName));
    return get ? get() : nullptr;
}

}  // namespace

uint64_t SteamP2P::localSteamId() {
    void* user = steamInterface("SteamUser");
    if (!user) return 0;
    uint64_t id = 0;
    vfn<GetSteamID_t>(user, 2)(user, &id);
    return id;
}

bool SteamP2P::open(uint64_t peer) {
    net_ = steamInterface("SteamNetworking");
    peer_ = peer;
    if (!net_) {
        log::error("net: steam_api.dll sem SteamNetworking (jogo aberto fora da Steam?)");
        return false;
    }
    if (!peer_) {
        log::error("net: transport=steam sem steam_peer (SteamID64 do amigo) no sodcoop.ini");
        return false;
    }
    vfn<AllowRelay_t>(net_, 7)(net_, true);
    vfn<Accept_t>(net_, 3)(net_, peer_);
    lastAcceptMs_ = GetTickCount();
    return true;
}

void SteamP2P::close() { net_ = nullptr; }

bool SteamP2P::sendTo(const Address& to, const void* data, int size) {
    if (!net_ || !(to == kPeerAddress) || size <= 0) return false;
    const int mode = static_cast<uint32_t>(size) > kUnreliableMax ? kSendReliable : kSendUnreliable;
    return vfn<Send_t>(net_, 0)(net_, peer_, data, static_cast<uint32_t>(size), mode, kChannel);
}

int SteamP2P::receive(void* buffer, int capacity, Address& from) {
    if (!net_) return -1;
    // Sem o callback P2PSessionRequest_t: aceita a sessão do amigo de tempos em tempos (vale
    // também se a Steam a fechou por inatividade).
    const uint32_t now = GetTickCount();
    if (now - lastAcceptMs_ > 2000) {
        vfn<Accept_t>(net_, 3)(net_, peer_);
        lastAcceptMs_ = now;
    }
    for (int guard = 0; guard < 64; ++guard) {
        uint32_t size = 0;
        if (!vfn<Available_t>(net_, 1)(net_, &size, kChannel)) return 0;
        uint32_t got = 0;
        uint64_t sender = 0;
        static char discard[65536];
        void* dest = size <= static_cast<uint32_t>(capacity) ? buffer : discard;
        const uint32_t cap = size <= static_cast<uint32_t>(capacity) ? capacity : sizeof(discard);
        if (!vfn<Read_t>(net_, 2)(net_, dest, cap, &got, &sender, kChannel)) return 0;
        if (sender != peer_ || dest == discard) continue;  // outro remetente ou grande demais
        from = kPeerAddress;
        return static_cast<int>(got);
    }
    return 0;
}

}  // namespace sodcoop::net
