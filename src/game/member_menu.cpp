#include "game/member_menu.h"

#include <windows.h>

#include <cctype>
#include <cstdio>
#include <cstring>
#include <string>

#include "core/log.h"
#include "game/engine.h"

namespace sodcoop::member_menu {
namespace {

// Lista de membros da "família" (o elenco de sobreviventes que fm_nextMember percorre).
constexpr uintptr_t kMemberCount = 0x01E65864;    // u32
constexpr uintptr_t kMemberArray = 0x01E65874;    // ponteiro; registros de 0x34 bytes
constexpr uintptr_t kCurrentMember = 0x01E658C8;  // ponteiro do membro atual; índice u16 em +0xF4
constexpr size_t kMemberStride = 0x34;
constexpr size_t kMember_BodyPath = 0x08;  // const char* .cdf do corpo
constexpr size_t kCurrent_Index = 0xF4;
constexpr int kVisibleLines = 11;

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

int memberCount() {
    uint32_t n = 0;
    return readAt(kMemberCount, n) && n > 0 && n < 1000 ? static_cast<int>(n) : 0;
}

int currentIndex() {
    uintptr_t cur = 0;
    uint16_t idx = 0;
    if (!readAt(kCurrentMember, cur) || !cur || !readAt(cur + kCurrent_Index, idx)) return -1;
    return idx;
}

// "Mulher - colete e jeans" a partir de "characters/female/human/body/tshirtvest_jeans/...".
// Só ASCII: a fonte do jogo não desenha acentos.
std::string label(int index) {
    uintptr_t arr = 0;
    const char* path = nullptr;
    if (!readAt(kMemberArray, arr) || !arr ||
        !readAt(arr + static_cast<uintptr_t>(index) * kMemberStride + kMember_BodyPath, path) ||
        !engine::isReadable(path, 1))
        return "?";
    std::string p(path, strnlen(path, 200));
    std::string who = p.find("/female/") != std::string::npos ? "Mulher" : p.find("/male/") != std::string::npos ? "Homem" : "?";
    std::string outfit;
    const std::string key = "/body/";
    const size_t b = p.find(key);
    if (b != std::string::npos) {
        const size_t e = p.find('/', b + key.size());
        outfit = p.substr(b + key.size(), e == std::string::npos ? std::string::npos : e - b - key.size());
    }
    static const struct {
        const char* from;
        const char* to;
    } words[] = {
        {"leatherjacket", "jaqueta de couro"}, {"denimjacket", "jaqueta jeans"}, {"tshirtvest", "colete"},
        {"tshirt", "camiseta"}, {"sweater", "sueter"}, {"hoodie", "moletom"}, {"jacket", "jaqueta"},
        {"buttons", "camisa de botoes"}, {"flannel", "camisa xadrez"}, {"camo", "camuflado"},
        {"vest", "colete"}, {"jeans", "jeans"}, {"leggings", "legging"}, {"shorts", "bermuda"},
        {"skirt", "saia"}, {"dress", "vestido"}, {"uniform", "uniforme"}, {"tank", "regata"},
        {"polo", "polo"}, {"suit", "terno"}, {"coat", "casaco"}, {"scrubs", "jaleco"},
    };
    std::string pretty;
    size_t start = 0;
    while (start <= outfit.size()) {
        size_t end = outfit.find('_', start);
        if (end == std::string::npos) end = outfit.size();
        std::string part = outfit.substr(start, end - start);
        while (!part.empty() && isdigit(static_cast<unsigned char>(part.back()))) part.pop_back();
        std::string word = part;
        for (const auto& w : words)
            if (part == w.from) {
                word = w.to;
                break;
            }
        if (!word.empty()) pretty += (pretty.empty() ? "" : " e ") + word;
        start = end + 1;
    }
    for (char& c : pretty)
        if (!isalnum(static_cast<unsigned char>(c)) && c != ' ') c = ' ';
    return who + (pretty.empty() ? "" : " - " + pretty);
}

// Texto 2D de depuração do próprio jogo (o mesmo do "Family Stats" do fm_stats):
// cdecl (IRenderer*, float x, float y, float tamanho, const float* rgba, bool centralizar,
// const char* formato, ...), em pixels; IRenderer = gEnv+0x10. System.DrawText (Lua) não
// aparece neste jogo, e System.DrawLabel (ponto 3D) acompanhava a câmera com atraso.
constexpr uintptr_t kDraw2dText = 0x00473160;
constexpr size_t kGEnv_Renderer = 0x10;

void draw(int count, int current) {
    void* renderer = engine::gEnvField(kGEnv_Renderer);
    if (!engine::looksLikeObject(renderer)) return;
    using Draw_t = void(__cdecl*)(void*, float, float, float, const float*, bool, const char*, ...);
    const auto drawText = reinterpret_cast<Draw_t>(kDraw2dText);
    char buf[256];
    auto line = [&](float y, const std::string& text, float size, float r, float g, float b) {
        const float rgba[4] = {r, g, b, 1.0f};
        drawText(renderer, 60.0f, y, size, rgba, false, "%s", text.c_str());
    };
    line(90, "ESCOLHER SOBREVIVENTE", 2.2f, 1, 1, 1);
    line(115, "setas: escolher   PgUp/PgDn: pular 10   Enter: trocar   F7: fechar", 1.5f, 0.75f, 0.75f, 0.75f);
    int first = g_selected - kVisibleLines / 2;
    if (first > count - kVisibleLines) first = count - kVisibleLines;
    if (first < 0) first = 0;
    for (int k = 0; k < kVisibleLines && first + k < count; ++k) {
        const int i = first + k;
        std::string text = std::string(i == g_selected ? "> " : "  ") + std::to_string(i) + "  " + label(i) +
                           (i == current ? "   (atual)" : "");
        const bool sel = i == g_selected;
        line(150.0f + k * 24.0f, text, 1.8f, sel ? 1.0f : 0.85f, sel ? 0.75f : 0.85f, sel ? 0.2f : 0.85f);
    }
    snprintf(buf, sizeof(buf), "%d de %d", g_selected + 1, count);
    line(150.0f + kVisibleLines * 24.0f + 8, buf, 1.5f, 0.75f, 0.75f, 0.75f);
}

}  // namespace

void update(int toggleKey, bool settled, bool dead) {
    DWORD pid = 0;
    GetWindowThreadProcessId(GetForegroundWindow(), &pid);
    const bool focused = pid == GetCurrentProcessId();
    // Lê as teclas sempre (para as bordas ficarem certas), mas só age com a janela em foco.
    const bool toggle = pressed(toggleKey);
    const bool up = pressed(VK_UP), down = pressed(VK_DOWN), pgUp = pressed(VK_PRIOR), pgDn = pressed(VK_NEXT);
    const bool enter = pressed(VK_RETURN);
    const int count = memberCount();
    if (!settled || dead || count == 0) {
        g_open = false;
        return;
    }
    if (focused && toggle) {
        g_open = !g_open;
        if (g_open) {
            const int cur = currentIndex();
            g_selected = cur >= 0 ? cur : 0;
        }
    }
    if (!g_open) return;
    if (focused) {
        if (up) --g_selected;
        if (down) ++g_selected;
        if (pgUp) g_selected -= 10;
        if (pgDn) g_selected += 10;
        if (g_selected < 0) g_selected = count - 1;
        if (g_selected >= count) g_selected = 0;
        if (enter) {
            char code[80];
            snprintf(code, sizeof(code), "System.ExecuteCommand(\"fm_nextMember %d\")", g_selected);
            const bool ok = engine::executeLua(code, "sodcoop_member");
            log::info("sobrevivente: menu -> membro %d (%s) -> %s", g_selected, label(g_selected).c_str(),
                      ok ? "OK" : "FALHOU");
            g_open = false;
            return;
        }
    }
    draw(count, currentIndex());
}

}  // namespace sodcoop::member_menu
