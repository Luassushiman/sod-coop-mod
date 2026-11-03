#include "game/give_menu.h"

#include <windows.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "core/log.h"
#include "game/engine.h"
#include "net/session.h"

namespace sodcoop::give_menu {
namespace {

// Mochila do sobrevivente atual: NSInventory::Inventory (vtable 0x0189AE98), ver
// docs/RE_ENDERECOS.md, 4h. +4 u32 vagas; +0x10 u32* id do item por vaga (0 = vazia);
// +0x14 dados da vaga (6 bytes: quantidade nos empilháveis; durabilidade/munição nas armas).
// É a do registro do membro atual (a que a tela mostra); *(0x01E658F0) é outra cópia, e
// mexer só nela não muda a mochila.
constexpr uintptr_t kCurrentMember = 0x01E658C8;
constexpr size_t kMember_Inventory = 0xE8;
constexpr uintptr_t kInventoryVtable = 0x0189AE98;
constexpr size_t kInv_Slots = 0x04, kInv_Ids = 0x10, kInv_Data = 0x14;
constexpr size_t kSlotData = 6;
// Métodos (__thiscall) pela vtable.
constexpr size_t kVt_AddStack = 0x0C;   // int (id, qtd): empilháveis (tipo 1 munição, 3 consumível)
constexpr size_t kVt_ClearSlot = 0x14;  // void (vaga)
constexpr size_t kVt_SetSlot = 0x18;    // void (vaga, id): zera os dados da vaga
constexpr size_t kVt_FreeSlot = 0x1C;   // uint (): primeira vaga vazia, -1 se cheia
// Tabelas de definição por tipo (id >> 28): {ponteiro, palavras por registro}. O nome
// interno das armas está em +0x24; consumíveis só têm o nome do som (+0x0C).
struct DefTable {
    uintptr_t ptr;
    size_t words;
};
constexpr DefTable kDefs[7] = {{0, 0},           {0x01E5AF60, 0xC},  {0x01E5AF68, 0xF}, {0x01E5AF6C, 0x17},
                               {0x01E5AF80, 0x22}, {0x01E5AF90, 0x1C}, {0x01E5AF9C, 9}};
constexpr int kVisibleLines = 11;
constexpr uintptr_t kDraw2dText = 0x00473160;  // ver member_menu.cpp
constexpr size_t kGEnv_Renderer = 0x10;

bool g_open = false;
int g_selected = 0;
bool g_keyDown[256] = {};

bool pressed(int vk) {
    if (vk <= 0 || vk > 0xFE) return false;
    const bool down = (GetAsyncKeyState(vk) & 0x8000) != 0;
    const bool edge = down && !g_keyDown[vk];
    g_keyDown[vk] = down;
    return edge;
}

template <typename T>
bool readAt(uintptr_t addr, T& out) {
    auto p = reinterpret_cast<const T*>(addr);
    if (!engine::isReadable(p, sizeof(T))) return false;
    out = *p;
    return true;
}

struct Inventory {
    uintptr_t obj = 0;
    uint32_t slots = 0;
    uint32_t* ids = nullptr;
    uint8_t* data = nullptr;
    void* method(size_t off) const { return reinterpret_cast<void**>(*reinterpret_cast<uintptr_t*>(obj))[off / 4]; }
};

bool playerInventory(Inventory& inv) {
    uintptr_t member = 0, vtable = 0, ids = 0, data = 0;
    if (!readAt(kCurrentMember, member) || !member || !readAt(member + kMember_Inventory, inv.obj) || !inv.obj ||
        !readAt(inv.obj, vtable) || vtable != kInventoryVtable || !readAt(inv.obj + kInv_Slots, inv.slots) ||
        inv.slots == 0 || inv.slots > 64 || !readAt(inv.obj + kInv_Ids, ids) || !readAt(inv.obj + kInv_Data, data) ||
        !engine::isReadable(reinterpret_cast<void*>(ids), inv.slots * 4) ||
        !engine::isReadable(reinterpret_cast<void*>(data), inv.slots * kSlotData))
        return false;
    inv.ids = reinterpret_cast<uint32_t*>(ids);
    inv.data = reinterpret_cast<uint8_t*>(data);
    return true;
}

bool stackable(uint32_t id) {
    const uint32_t type = id >> 28;
    return type == 1 || type == 3;
}

std::string cleanName(const char* s) {
    std::string out;
    for (; s && *s && out.size() < 40; ++s) out += (*s == '_' ? ' ' : *s);
    return out;
}

// Rótulo do item: nome interno das armas; nos outros, o nome do som ("meds", "ammo_shell").
std::string label(uint32_t id, const uint8_t* data) {
    const uint32_t type = id >> 28, index = id & 0x0FFFFFFF;
    const char* kind = type == 1 ? "Municao" : type == 2 ? "Mochila" : type == 3 ? "Item" : type == 4 ? "Arma de fogo"
                     : type == 5 ? "Arma"   : "Outro";
    std::string name;
    if (type >= 1 && type <= 6) {
        uintptr_t base = 0;
        if (readAt(kDefs[type].ptr, base) && base) {
            const uintptr_t rec = base + index * kDefs[type].words * 4;
            const char* str = nullptr;
            if ((type == 4 || type == 5) && readAt(rec + 0x24, str) && engine::isReadable(str, 1)) name = cleanName(str);
            if (name.empty() && readAt(rec + 0x0C, str) && engine::isReadable(str, 1)) {
                // "sounds/ui:ui_assign_item:meds_assign" -> "meds"
                std::string s(str, strnlen(str, 120));
                const size_t c = s.rfind(':');
                if (c != std::string::npos) s = s.substr(c + 1);
                const size_t u = s.rfind('_');
                if (u != std::string::npos) s = s.substr(0, u);
                name = cleanName(s.c_str());
            }
        }
    }
    char buf[96];
    if (stackable(id))
        snprintf(buf, sizeof(buf), "%s: %s (%u)", kind, name.empty() ? "?" : name.c_str(), data[0]);
    else
        snprintf(buf, sizeof(buf), "%s: %s", kind, name.empty() ? "?" : name.c_str());
    return buf;
}

// Põe o item na mochila daqui; false se não coube (nada muda).
bool addItem(const net::GiveEvent& g) {
    Inventory inv;
    if (!playerInventory(inv) || !g.itemId) return false;
    if (stackable(g.itemId)) {
        using AddStack_t = int(__thiscall*)(void*, uint32_t, int);
        const int want = g.data[0] ? g.data[0] : 1;
        const int added = reinterpret_cast<AddStack_t>(inv.method(kVt_AddStack))(reinterpret_cast<void*>(inv.obj),
                                                                                g.itemId, want);
        if (added == want) return true;
        if (added > 0) log::warn("give: so couberam %d de %d (o resto se perde)", added, want);
        return added > 0;
    }
    using FreeSlot_t = uint32_t(__thiscall*)(void*);
    using SetSlot_t = void(__thiscall*)(void*, uint32_t, uint32_t);
    const uint32_t slot = reinterpret_cast<FreeSlot_t>(inv.method(kVt_FreeSlot))(reinterpret_cast<void*>(inv.obj));
    if (slot >= inv.slots) return false;
    reinterpret_cast<SetSlot_t>(inv.method(kVt_SetSlot))(reinterpret_cast<void*>(inv.obj), slot, g.itemId);
    memcpy(inv.data + slot * kSlotData, g.data, kSlotData);  // durabilidade, munição...
    return true;
}

void giveSlot(int slot) {
    Inventory inv;
    if (!playerInventory(inv) || slot < 0 || slot >= static_cast<int>(inv.slots) || !inv.ids[slot]) return;
    net::GiveEvent g;
    g.itemId = inv.ids[slot];
    memcpy(g.data, inv.data + slot * kSlotData, kSlotData);
    const std::string what = label(g.itemId, g.data);
    using ClearSlot_t = void(__thiscall*)(void*, uint32_t);
    reinterpret_cast<ClearSlot_t>(inv.method(kVt_ClearSlot))(reinterpret_cast<void*>(inv.obj), slot);
    net::sendGive(g);
    log::info("give: dei %s (vaga %d, id %08X)", what.c_str(), slot, g.itemId);
}

void receive() {
    for (net::GiveEvent& g : net::takeGives()) {
        const std::string what = label(g.itemId, g.data);
        if (addItem(g)) {
            log::info("give: recebi %s (id %08X)%s", what.c_str(), g.itemId, g.returned ? " (devolvido)" : "");
        } else if (!g.returned) {
            g.returned = true;  // não coube: volta para quem deu
            net::sendGive(g);
            log::warn("give: %s nao coube na mochila; devolvido", what.c_str());
        } else {
            log::warn("give: %s voltou e nao coube mais aqui; perdido", what.c_str());
        }
    }
}

void draw(const Inventory& inv, const std::vector<int>& rows) {
    void* renderer = engine::gEnvField(kGEnv_Renderer);
    if (!engine::looksLikeObject(renderer)) return;
    using Draw_t = void(__cdecl*)(void*, float, float, float, const float*, bool, const char*, ...);
    const auto drawText = reinterpret_cast<Draw_t>(kDraw2dText);
    auto line = [&](float y, const std::string& text, float size, float r, float g, float b) {
        const float rgba[4] = {r, g, b, 1.0f};
        drawText(renderer, 60.0f, y, size, rgba, false, "%s", text.c_str());
    };
    line(90, "DAR ITEM AO OUTRO JOGADOR", 2.2f, 1, 1, 1);
    line(115, "setas: escolher   Enter: dar   F8: fechar", 1.5f, 0.75f, 0.75f, 0.75f);
    if (rows.empty()) {
        line(150, "mochila vazia", 1.8f, 0.85f, 0.85f, 0.85f);
        return;
    }
    const int count = static_cast<int>(rows.size());
    int first = g_selected - kVisibleLines / 2;
    if (first > count - kVisibleLines) first = count - kVisibleLines;
    if (first < 0) first = 0;
    for (int k = 0; k < kVisibleLines && first + k < count; ++k) {
        const int i = first + k;
        const int slot = rows[i];
        const bool sel = i == g_selected;
        line(150.0f + k * 24.0f, std::string(sel ? "> " : "  ") + label(inv.ids[slot], inv.data + slot * kSlotData),
             1.8f, sel ? 1.0f : 0.85f, sel ? 0.75f : 0.85f, sel ? 0.2f : 0.85f);
    }
}

}  // namespace

void update(int toggleKey, bool settled, bool dead, bool connected) {
    DWORD pid = 0;
    GetWindowThreadProcessId(GetForegroundWindow(), &pid);
    const bool focused = pid == GetCurrentProcessId();
    const bool toggle = pressed(toggleKey);
    const bool up = pressed(VK_UP), down = pressed(VK_DOWN), enter = pressed(VK_RETURN);
    if (!settled || !connected) {
        g_open = false;
        return;
    }
    if (!dead) receive();
    Inventory inv;
    if (dead || !playerInventory(inv)) {
        g_open = false;
        return;
    }
    if (focused && toggle) {
        g_open = !g_open;
        g_selected = 0;
    }
    if (!g_open) return;
    std::vector<int> rows;
    for (uint32_t s = 0; s < inv.slots; ++s)
        if (inv.ids[s]) rows.push_back(static_cast<int>(s));
    const int count = static_cast<int>(rows.size());
    if (focused && count) {
        if (up) --g_selected;
        if (down) ++g_selected;
        if (g_selected < 0) g_selected = count - 1;
        if (g_selected >= count) g_selected = 0;
        if (enter) {
            giveSlot(rows[g_selected]);
            g_open = false;
            return;
        }
    }
    draw(inv, rows);
}

}  // namespace sodcoop::give_menu
