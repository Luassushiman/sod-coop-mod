#include "game/base_sync.h"

#include <cstdint>
#include <cstring>
#include <vector>

#include "core/clock.h"
#include "core/log.h"
#include "game/engine.h"
#include "net/session.h"

namespace sodcoop::base_sync {
namespace {

// Armário de suprimentos: NSInventory::Inventory em *(registro da comunidade + 0x244)
// (vtable 0x0189AE98; +4 vagas, +0x10 ids, +0x14 dados de 6 bytes). Métodos como em give_menu.
constexpr uintptr_t kLockerPtr = 0x01E4BC30 + 0x244;
constexpr uintptr_t kInventoryVtable = 0x0189AE98;
constexpr size_t kInv_Slots = 0x04, kInv_Ids = 0x10, kInv_Data = 0x14;
constexpr size_t kSlotData = 6;
constexpr size_t kVt_ClearSlot = 0x14;  // void (vaga)
constexpr size_t kVt_SetSlot = 0x18;    // void (vaga, id): zera os dados da vaga
// Recursos do estoque: "resource data" tipo 9; grupos em *(0x01E1FBF4), dados em +0xC, uma
// entrada de 0x14 bytes por recurso com o valor (float codificado) em +8. Chave de XOR =
// *(0x01E1FDAC) ^ *(0x01E1FB54) (ver 0x01394740, que lê, e 0x013A3C30, que grava).
constexpr uintptr_t kResourceGroups = 0x01E1FBF4;
constexpr uintptr_t kKeyA = 0x01E1FDAC, kKeyB = 0x01E1FB54;
constexpr size_t kGroup_Data = 0x0C, kEntrySize = 0x14, kEntry_Value = 0x08;
constexpr int kResources = 5;

// Instalações: 32 vagas de 0xF0 em 0x01E545E8 (+0x0C tipo, -1 = vazia; +0x10 variante;
// +0x50 nome da vaga; +0x68 id do handle). Definições em *(0x01E8F1D8), 0x20 cada
// (+0x04 hash do FacilityId, +0x18 nº de variantes, +0x1C variantes de 0x28 com o hash
// do VariantId em +0x04). Ver docs/RE_ENDERECOS.md, 4i.
constexpr uintptr_t kSockets = 0x01E545E8;
constexpr size_t kSocketStride = 0xF0;
constexpr int kSocketCount = 32;
constexpr size_t kSock_Type = 0x0C, kSock_Variant = 0x10, kSock_Name = 0x50, kSock_HandleByte = 0x6A;
constexpr uintptr_t kFacilityDefs = 0x01E8F1D8, kFacilityDefCount = 0x01E8F1CC;
constexpr size_t kDefStride = 0x20, kDef_Hash = 0x04, kDef_Variants = 0x1C, kDef_VariantCount = 0x18;
constexpr size_t kVariantStride = 0x28, kVar_Hash = 0x04;
// NSHome::Facility::SetFacility (thiscall vaga, byte do handle, hash FacilityId, hash
// VariantId): monta a instalação sem gastar recursos (o jogo usa ao carregar o save).
constexpr uintptr_t kSetFacility = 0x015786C0;
// Demolir pela vaga: cdecl (handle). Handle = 3 << 27 | índice << 15 | id (15 bits).
constexpr uintptr_t kDestroyFacility = 0x01406520;

constexpr double kInterval = 0.5;      // s entre comparações
constexpr double kFullInterval = 5.0;  // s entre envios completos do host

template <typename T>
bool readAt(uintptr_t addr, T& out) {
    auto p = reinterpret_cast<const T*>(addr);
    if (!engine::isReadable(p, sizeof(T))) return false;
    out = *p;
    return true;
}

struct Locker {
    uintptr_t obj = 0;
    uint32_t slots = 0;
    uint32_t* ids = nullptr;
    uint8_t* data = nullptr;
    void* method(size_t off) const { return reinterpret_cast<void**>(*reinterpret_cast<uintptr_t*>(obj))[off / 4]; }
};

bool findLocker(Locker& l) {
    uintptr_t vtable = 0, ids = 0, data = 0;
    if (!readAt(kLockerPtr, l.obj) || !l.obj || !readAt(l.obj, vtable) || vtable != kInventoryVtable ||
        !readAt(l.obj + kInv_Slots, l.slots) || l.slots == 0 || l.slots > 512 || !readAt(l.obj + kInv_Ids, ids) ||
        !readAt(l.obj + kInv_Data, data) || !engine::isReadable(reinterpret_cast<void*>(ids), l.slots * 4) ||
        !engine::isReadable(reinterpret_cast<void*>(data), l.slots * kSlotData))
        return false;
    l.ids = reinterpret_cast<uint32_t*>(ids);
    l.data = reinterpret_cast<uint8_t*>(data);
    return true;
}

net::LockerSlot slotOf(const Locker& l, uint32_t i) {
    net::LockerSlot s;
    s.slot = static_cast<uint16_t>(i);
    s.id = l.ids[i];
    memcpy(s.data, l.data + i * kSlotData, kSlotData);
    return s;
}

bool same(const net::LockerSlot& a, const net::LockerSlot& b) {
    return a.id == b.id && memcmp(a.data, b.data, kSlotData) == 0;
}

void writeSlot(const Locker& l, const net::LockerSlot& s) {
    using ClearSlot_t = void(__thiscall*)(void*, uint32_t);
    using SetSlot_t = void(__thiscall*)(void*, uint32_t, uint32_t);
    void* obj = reinterpret_cast<void*>(l.obj);
    if (s.id == 0) {
        if (l.ids[s.slot]) reinterpret_cast<ClearSlot_t>(l.method(kVt_ClearSlot))(obj, s.slot);
        return;
    }
    if (l.ids[s.slot] != s.id) {
        if (l.ids[s.slot]) reinterpret_cast<ClearSlot_t>(l.method(kVt_ClearSlot))(obj, s.slot);
        reinterpret_cast<SetSlot_t>(l.method(kVt_SetSlot))(obj, s.slot, s.id);
    }
    memcpy(l.data + s.slot * kSlotData, s.data, kSlotData);
}

// Troca de bits do jogo (cada passo troca pares de bits: é a própria inversa).
uint32_t shuffle1(uint32_t u) { return ((u >> 1) & 0x22222222u) | ((u & 0x22222222u) << 1) | (u & 0x99999999u); }
uint32_t shuffle2(uint32_t u) { return ((u >> 2) & 0x0c0c0c0cu) | ((u & 0x0c0c0c0cu) << 2) | (u & 0xc3c3c3c3u); }
uint32_t shuffle3(uint32_t u) { return ((u >> 4) & 0x00f000f0u) | ((u & 0x00f000f0u) << 4) | (u & 0xf00ff00fu); }
uint32_t shuffle4(uint32_t u) { return ((u >> 8) & 0x0000ff00u) | ((u & 0x0000ff00u) << 8) | (u & 0xff0000ffu); }

bool resourceSlot(int i, uint32_t*& slot, uint32_t& key) {
    uintptr_t groups = 0, data = 0;
    uint32_t a = 0, b = 0;
    if (!readAt(kResourceGroups, groups) || !groups || !readAt(groups + kGroup_Data, data) || !data ||
        !readAt(kKeyA, a) || !readAt(kKeyB, b))
        return false;
    slot = reinterpret_cast<uint32_t*>(data + i * kEntrySize + kEntry_Value);
    if (!engine::isReadable(slot, 4)) return false;
    key = a ^ b;
    return true;
}

bool readResources(float out[kResources]) {
    for (int i = 0; i < kResources; ++i) {
        uint32_t* slot;
        uint32_t key;
        if (!resourceSlot(i, slot, key)) return false;
        const uint32_t u = shuffle4(shuffle3(shuffle2(shuffle1(*slot)))) ^ key;
        memcpy(&out[i], &u, 4);
    }
    return true;
}

bool writeResource(int i, float value) {
    uint32_t* slot;
    uint32_t key;
    if (!resourceSlot(i, slot, key)) return false;
    uint32_t u;
    memcpy(&u, &value, 4);
    *slot = shuffle1(shuffle2(shuffle3(shuffle4(u ^ key))));
    return true;
}

uintptr_t socketAt(int i) { return kSockets + static_cast<uintptr_t>(i) * kSocketStride; }

bool socketExists(int i) {
    uintptr_t name = 0;
    return readAt(socketAt(i) + kSock_Name, name) && name != 0;
}

// Hashes da instalação de uma vaga (0, 0 = vazia ou ilegível).
void readFacility(int i, uint32_t& fac, uint32_t& var) {
    fac = var = 0;
    int32_t type = -1, variant = 0;
    uintptr_t defs = 0, variants = 0;
    uint32_t count = 0, nvar = 0;
    if (!socketExists(i) || !readAt(socketAt(i) + kSock_Type, type) || type < 0 ||
        !readAt(socketAt(i) + kSock_Variant, variant) || !readAt(kFacilityDefs, defs) || !defs ||
        !readAt(kFacilityDefCount, count) || static_cast<uint32_t>(type) >= count)
        return;
    const uintptr_t def = defs + type * kDefStride;
    if (!readAt(def + kDef_Hash, fac)) return;
    if (readAt(def + kDef_VariantCount, nvar) && variant >= 0 && static_cast<uint32_t>(variant) < nvar &&
        readAt(def + kDef_Variants, variants) && variants)
        readAt(variants + variant * kVariantStride + kVar_Hash, var);
}

void readFacilities(uint32_t fac[kSocketCount], uint32_t var[kSocketCount]) {
    for (int i = 0; i < kSocketCount; ++i) readFacility(i, fac[i], var[i]);
}

// Põe na vaga i a instalação (fac, var) do outro lado; fac = 0 só demole.
void applyFacility(int i, uint32_t fac, uint32_t var) {
    const uintptr_t s = socketAt(i);
    int32_t type = -1;
    readAt(s + kSock_Type, type);
    if (type >= 0) {
        uint16_t id = 0;
        readAt(s + 0x68, id);
        const uint32_t handle = (3u << 27) | (static_cast<uint32_t>(i) << 15) | (id & 0x7fffu);
        reinterpret_cast<void(__cdecl*)(uint32_t)>(kDestroyFacility)(handle);
    }
    if (fac == 0) return;
    uint8_t handleByte = 0;
    readAt(s + kSock_HandleByte, handleByte);
    using SetFacility_t = void(__thiscall*)(void*, uint8_t, uint32_t, uint32_t);
    reinterpret_cast<SetFacility_t>(kSetFacility)(reinterpret_cast<void*>(s), handleByte, fac, var);
}

uint32_t g_syncedFac[kSocketCount] = {}, g_syncedVar[kSocketCount] = {};

bool g_have = false;                      // já tem a última versão combinada
std::vector<net::LockerSlot> g_synced;    // armário combinado com o outro lado
float g_syncedRes[kResources] = {};
double g_next = 0, g_nextFull = 0;
int g_logs = 0;

void sendFull(const Locker& l, const float res[kResources]) {
    std::vector<net::LockerSlot> all;
    for (uint32_t i = 0; i < l.slots; ++i) all.push_back(slotOf(l, i));
    net::sendLocker(all);
    net::sendResources(res);
    uint32_t fac[kSocketCount], var[kSocketCount];
    readFacilities(fac, var);
    net::sendFacilities(fac, var);
}

// Cancelar a construção pela vaga: cdecl (handle); devolve o custo (FUN_01571630).
constexpr uintptr_t kCancelFacility = 0x01405DA0;

}  // namespace

void finishFacility(int i) {
    if (i < 0 || i >= kSocketCount || !socketExists(i)) {
        log::warn("base_sync: vaga %d invalida", i);
        return;
    }
    const uintptr_t s = socketAt(i);
    uint32_t fac = 0, var = 0, flags = 0;
    readFacility(i, fac, var);
    readAt(s + 0x04, flags);
    if (!fac || !(flags & 1)) {
        log::warn("base_sync: vaga %d sem construcao em andamento (fac %08x flags %x)", i, fac, flags);
        return;
    }
    float before[kResources];
    const bool haveBefore = readResources(before);
    uint16_t id = 0;
    readAt(s + 0x68, id);
    const uint32_t handle = (3u << 27) | (static_cast<uint32_t>(i) << 15) | (id & 0x7fffu);
    reinterpret_cast<void(__cdecl*)(uint32_t)>(kCancelFacility)(handle);
    // var = 0: o SetFacility escolhe a 1ª variante que não é "em construção" (a pronta).
    applyFacility(i, fac, 0);
    if (haveBefore)
        for (int k = 0; k < kResources; ++k) writeResource(k, before[k]);
    uint32_t nf = 0, nv = 0;
    readFacility(i, nf, nv);
    readAt(s + 0x04, flags);
    log::info("base_sync: vaga %d terminada na hora: %08x/%08x -> %08x/%08x flags %x", i, fac, var, nf, nv, flags);
}

void update(bool active, bool host) {
    const double now = clock::seconds();
    if (!active) {
        g_have = false;
        return;
    }
    if (now < g_next) return;
    g_next = now + kInterval;
    Locker l;
    float res[kResources];
    if (!findLocker(l) || !readResources(res)) {
        g_have = false;  // sem base (ou ainda carregando)
        return;
    }
    if (!g_have || g_synced.size() != l.slots) {
        g_synced.clear();
        for (uint32_t i = 0; i < l.slots; ++i) g_synced.push_back(slotOf(l, i));
        memcpy(g_syncedRes, res, sizeof(res));
        readFacilities(g_syncedFac, g_syncedVar);
        g_have = true;
        log::info("base_sync: armario (%u vagas) e recursos %.0f/%.0f/%.0f/%.0f/%.0f", l.slots, res[0], res[1], res[2],
                  res[3], res[4]);
        if (host) {
            sendFull(l, res);
            g_nextFull = now + kFullInterval;
        }
        net::takeLocker();  // pacotes de antes de ter o estado daqui não valem
        net::takeResources(res);
        {
            uint32_t f[kSocketCount], v[kSocketCount];
            net::takeFacilities(f, v);
        }
        readResources(res);
        return;
    }

    // 1) O que chegou do outro lado. Vaga com mudança daqui ainda não enviada: fica a daqui
    //    (vai na etapa 2 e prevalece lá).
    int applied = 0;
    for (const net::LockerSlot& s : net::takeLocker()) {
        if (s.slot >= l.slots) continue;
        const net::LockerSlot mine = slotOf(l, s.slot);
        if (!same(mine, g_synced[s.slot]) || same(mine, s)) {
            if (same(mine, s)) g_synced[s.slot] = s;
            continue;
        }
        writeSlot(l, s);
        g_synced[s.slot] = slotOf(l, s.slot);
        ++applied;
    }
    if (applied && g_logs++ < 40) log::info("base_sync: %d vaga(s) do armario atualizada(s) pelo outro jogador", applied);
    float in[kResources];
    if (net::takeResources(in)) {
        bool changed = false;
        for (int i = 0; i < kResources; ++i) {
            const bool pending = res[i] != g_syncedRes[i];
            if (pending || in[i] == res[i]) continue;
            if (writeResource(i, in[i])) {
                res[i] = g_syncedRes[i] = in[i];
                changed = true;
            }
        }
        if (changed && g_logs++ < 40)
            log::info("base_sync: recursos do outro jogador: %.0f/%.0f/%.0f/%.0f/%.0f", res[0], res[1], res[2], res[3],
                      res[4]);
    }

    // Instalações do outro lado: vaga sem mudança daqui ainda não enviada recebe a de lá.
    {
        uint32_t inFac[kSocketCount], inVar[kSocketCount], fac[kSocketCount], var[kSocketCount];
        if (net::takeFacilities(inFac, inVar)) {
            readFacilities(fac, var);
            // Montar a instalação aqui não pode cobrar de novo: o custo já saiu do outro lado
            // e chega pelos recursos. Em 2026-11-04 o client descontou o combustível outra vez.
            float before[kResources];
            const bool haveBefore = readResources(before);
            bool appliedAny = false;
            for (int i = 0; i < kSocketCount; ++i) {
                if (!socketExists(i)) continue;
                const bool pending = fac[i] != g_syncedFac[i] || var[i] != g_syncedVar[i];
                if (pending || (fac[i] == inFac[i] && var[i] == inVar[i])) continue;
                applyFacility(i, inFac[i], inVar[i]);
                appliedAny = true;
                readFacility(i, fac[i], var[i]);
                // Fica o que o jogo aceitou (se recusou, não briga: só tenta de novo no próximo envio).
                g_syncedFac[i] = fac[i];
                g_syncedVar[i] = var[i];
                if (g_logs++ < 40) {
                    const char* name = "";
                    readAt(socketAt(i) + kSock_Name, name);
                    log::info("base_sync: vaga %d (%s) do outro jogador: %08x/%08x -> ficou %08x/%08x", i,
                              engine::isReadable(name, 1) ? name : "?", inFac[i], inVar[i], fac[i], var[i]);
                }
            }
            float after[kResources];
            if (appliedAny && haveBefore && readResources(after))
                for (int k = 0; k < kResources; ++k)
                    if (after[k] != before[k]) {
                        writeResource(k, before[k]);
                        if (g_logs++ < 40)
                            log::info("base_sync: recurso %d voltou de %.0f para %.0f (montar a instalacao cobrou)", k,
                                      after[k], before[k]);
                    }
        }
    }

    // 2) O que mudou daqui: manda (3 cópias; aplicar de novo não muda nada).
    std::vector<net::LockerSlot> changedSlots;
    for (uint32_t i = 0; i < l.slots; ++i) {
        const net::LockerSlot s = slotOf(l, i);
        if (!same(s, g_synced[i])) {
            changedSlots.push_back(s);
            g_synced[i] = s;
        }
    }
    if (!changedSlots.empty()) {
        for (int k = 0; k < 3; ++k) net::sendLocker(changedSlots);
        if (g_logs++ < 40) log::info("base_sync: %zu vaga(s) do armario mudaram aqui; enviando", changedSlots.size());
    }
    bool resChanged = false;
    for (int i = 0; i < kResources; ++i)
        if (res[i] != g_syncedRes[i]) resChanged = true;
    if (resChanged) {
        memcpy(g_syncedRes, res, sizeof(res));
        for (int k = 0; k < 3; ++k) net::sendResources(res);
        if (g_logs++ < 40)
            log::info("base_sync: recursos mudaram aqui: %.0f/%.0f/%.0f/%.0f/%.0f", res[0], res[1], res[2], res[3],
                      res[4]);
    }

    {
        uint32_t fac[kSocketCount], var[kSocketCount];
        readFacilities(fac, var);
        bool changed = false;
        for (int i = 0; i < kSocketCount; ++i)
            if (fac[i] != g_syncedFac[i] || var[i] != g_syncedVar[i]) {
                if (g_logs++ < 40) log::info("base_sync: vaga %d mudou aqui: %08x/%08x", i, fac[i], var[i]);
                changed = true;
            }
        if (changed) {
            memcpy(g_syncedFac, fac, sizeof(fac));
            memcpy(g_syncedVar, var, sizeof(var));
            for (int k = 0; k < 3; ++k) net::sendFacilities(fac, var);
        }
    }

    // 3) Host: tudo a cada 5 s (o cliente acerta o que um pacote perdido deixou diferente).
    if (host && now >= g_nextFull) {
        g_nextFull = now + kFullInterval;
        sendFull(l, res);
    }
}

}  // namespace sodcoop::base_sync
