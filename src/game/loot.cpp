#include "game/loot.h"

#include <cstdint>
#include <cstring>
#include <map>
#include <vector>

#include "core/clock.h"
#include "core/log.h"
#include "game/engine.h"
#include "net/protocol.h"
#include "net/session.h"

namespace sodcoop::loot {
namespace {

// Mundo RTS (ver docs/RE_ENDERECOS.md, 4g). NSRTSWorld: RTSWorldBuildingSetSearched
// (0x013B6C20) -> 0x013AB350 percorre os nós do prédio e faz flags[nó] |= 1.
constexpr uintptr_t kRtsNodes = 0x01E46A98;  // ponteiro para a tabela de nós
// +0x10: u16* prédio de cada nó (não usado aqui)
constexpr size_t kNodes_Flags = 0x20;        // u8* flags de cada nó; bit 0 = vasculhado
constexpr size_t kNodes_Count = 0x24;        // u16 quantidade de nós
constexpr uint8_t kFlagSearched = 0x01;

// Histórico de busca: 20 entradas de 0x68 bytes, uma por lugar vasculhado recentemente,
// com o que sobrou dentro. +0 int nó (-1 = livre), +4 u32 quantidade de itens,
// +8 u32 ids[16], +0x48 u8 quantidades[16], +0x58 u8 tipos[16] (0 item, 1 recurso).
constexpr uintptr_t kHistory = 0x01E450D0;
constexpr int kHistoryEntries = 20;
constexpr size_t kEntrySize = 0x68;
constexpr size_t kEntry_Count = 0x04, kEntry_Ids = 0x08, kEntry_Counts = 0x48, kEntry_Types = 0x58;
// cdecl(int nó) -> entrada: pega a próxima do anel (devolve ao lugar os recursos da antiga).
constexpr uintptr_t kAllocEntry = 0x013AD200;
// cdecl(entrada, n, bool devolver): tira (false) ou devolve (true) à tabela de recursos dos
// lugares os lotes de recurso das entradas. A busca do jogo chama com false ao criar.
constexpr uintptr_t kAdjustResources = 0x013AD3C0;
using AllocEntryFn = uint8_t*(__cdecl*)(int);
using AdjustResourcesFn = void(__cdecl*)(uint8_t*, int, char);

template <class T>
bool readAt(uintptr_t addr, T& out) {
    const auto* p = reinterpret_cast<const T*>(addr);
    if (!engine::isReadable(p, sizeof(T))) return false;
    out = *p;
    return true;
}

// Flags dos nós no mundo atual (nullptr se o mundo não está carregado).
uint8_t* nodeFlags(uint16_t& count) {
    uintptr_t nodes = 0, flags = 0;
    count = 0;
    if (!readAt(kRtsNodes, nodes) || !nodes || !readAt(nodes + kNodes_Count, count) || !count ||
        count > net::kSearchedBytes * 8 || !readAt(nodes + kNodes_Flags, flags) || !flags ||
        !engine::isReadable(reinterpret_cast<void*>(flags), count))
        return nullptr;
    return reinterpret_cast<uint8_t*>(flags);
}

uint8_t* entryAt(int i) { return reinterpret_cast<uint8_t*>(kHistory + i * kEntrySize); }
int32_t entryNode(const uint8_t* e) { return *reinterpret_cast<const int32_t*>(e); }

uint8_t* findEntry(int node) {
    for (int i = 0; i < kHistoryEntries; ++i)
        if (entryNode(entryAt(i)) == node) return entryAt(i);
    return nullptr;
}

void readEntry(const uint8_t* e, net::ContainerState& c) {
    c.node = entryNode(e);
    c.count = *reinterpret_cast<const uint32_t*>(e + kEntry_Count);
    if (c.count > net::kContainerSlots) c.count = net::kContainerSlots;
    memcpy(c.ids, e + kEntry_Ids, sizeof(c.ids));
    memcpy(c.counts, e + kEntry_Counts, sizeof(c.counts));
    memcpy(c.types, e + kEntry_Types, sizeof(c.types));
}

bool sameContent(const net::ContainerState& a, const net::ContainerState& b) {
    return a.count == b.count && !memcmp(a.ids, b.ids, sizeof(a.ids)) && !memcmp(a.counts, b.counts, sizeof(a.counts)) &&
           !memcmp(a.types, b.types, sizeof(a.types));
}

std::map<int, net::ContainerState> g_known;  // último conteúdo conhecido (daqui ou de lá) por nó
double g_next = 0, g_nextResend = 0;

// Conteúdo recebido mais novo que o conhecido: grava na entrada do nó (criando uma, se
// preciso, pelo mesmo caminho da busca do jogo) e marca o lugar como vasculhado.
// Versões iguais com conteúdo diferente (os dois começaram do mesmo jeito): vale o do host.
void applyRemote(const net::ContainerState& c, uint8_t* flags, uint16_t count, bool host) {
    if (c.node < 0 || c.node >= count) return;
    auto it = g_known.find(c.node);
    if (it != g_known.end()) {
        const bool tieToHost = !host && c.version == it->second.version && !sameContent(c, it->second);
        if (c.version < it->second.version || (c.version == it->second.version && !tieToHost)) return;
    }
    uint8_t* e = findEntry(c.node);
    const auto adjust = reinterpret_cast<AdjustResourcesFn>(kAdjustResources);
    if (e) {
        adjust(e, 1, 1);  // devolve os recursos do conteúdo antigo antes de trocar
    } else {
        e = reinterpret_cast<AllocEntryFn>(kAllocEntry)(c.node);
        if (!e || entryNode(e) != c.node) return;
    }
    *reinterpret_cast<uint32_t*>(e + kEntry_Count) = c.count;
    memcpy(e + kEntry_Ids, c.ids, sizeof(c.ids));
    memcpy(e + kEntry_Counts, c.counts, sizeof(c.counts));
    memcpy(e + kEntry_Types, c.types, sizeof(c.types));
    adjust(e, 1, 0);
    flags[c.node] |= kFlagSearched;
    g_known[c.node] = c;
    log::info("loot: lugar %d com o conteudo do outro jogador (%u item(ns), versao %u)", c.node, c.count, c.version);
}

}  // namespace

void update(bool active, bool host) {
    if (!active) {
        g_known.clear();
        return;
    }
    const double now = clock::seconds();
    if (now < g_next) return;
    g_next = now + 0.5;

    uint16_t count = 0;
    uint8_t* flags = nodeFlags(count);
    if (!flags) return;

    // Do outro lado: marca aqui o que ele já vasculhou (mesmo nível = mesma quantidade de nós).
    std::vector<uint8_t> remote;
    const uint16_t remoteCount = net::getRemoteSearched(remote);
    const bool sameWorld = remoteCount == count;
    if (remoteCount) {
        static bool warned = false;
        if (!sameWorld) {
            if (!warned) log::warn("loot: o outro lado tem %u nos e este %u; nao sincroniza", remoteCount, count);
            warned = true;
        } else {
            int marked = 0, first = -1;
            for (int n = 0; n < count && n / 8 < static_cast<int>(remote.size()); ++n) {
                if ((remote[n / 8] >> (n % 8) & 1) && !(flags[n] & kFlagSearched)) {
                    flags[n] |= kFlagSearched;
                    if (first < 0) first = n;
                    ++marked;
                }
            }
            if (marked) log::info("loot: %d lugar(es) vasculhado(s) pelo outro jogador marcado(s) aqui (no %d...)", marked, first);
        }
    }

    // Conteúdos: aplica os recebidos e manda os que mudaram aqui (e todos a cada 2 s).
    std::vector<net::ContainerState> in = net::takeContainers();
    if (sameWorld)
        for (const net::ContainerState& c : in) applyRemote(c, flags, count, host);
    std::vector<net::ContainerState> out;
    const bool resend = now >= g_nextResend;
    if (resend) g_nextResend = now + 2.0;
    for (int i = 0; i < kHistoryEntries; ++i) {
        const uint8_t* e = entryAt(i);
        const int node = entryNode(e);
        if (node < 0 || node >= count) continue;
        net::ContainerState c;
        readEntry(e, c);
        auto it = g_known.find(node);
        if (it == g_known.end() || !sameContent(it->second, c)) {
            c.version = it == g_known.end() ? 1 : it->second.version + 1;
            g_known[node] = c;
            out.push_back(c);
            log::info("loot: lugar %d mudou aqui (%u item(ns), versao %u)", node, c.count, c.version);
        } else if (resend) {
            out.push_back(it->second);
        }
    }
    if (!out.empty()) net::sendContainers(out);

    // Daqui: mapa de bits para a rede.
    std::vector<uint8_t> bits((count + 7) / 8, 0);
    int searched = 0;
    for (int n = 0; n < count; ++n) {
        if (flags[n] & kFlagSearched) {
            bits[n / 8] |= static_cast<uint8_t>(1 << (n % 8));
            ++searched;
        }
    }
    net::setLocalSearched(count, bits);
    static int lastSearched = -1;
    if (searched != lastSearched) {
        if (lastSearched >= 0) log::info("loot: %d de %u lugares vasculhados aqui", searched, count);
        lastSearched = searched;
    }
}

}  // namespace sodcoop::loot
