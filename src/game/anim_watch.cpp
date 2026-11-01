#include "game/anim_watch.h"

#include <windows.h>

#include <cstdio>
#include <cctype>
#include <cstring>
#include <string>

#include "core/log.h"

namespace sodcoop::anim_watch {

namespace {

using namespace engine;

// Layout do CSkeletonAnim (build 845221), ver docs/RE_ENDERECOS.md seção 4c.
// GetNumAnimsInFIFO (vtbl +0x3C, 0x00B136F0) e GetAnimFromFIFO (vtbl +0x44, 0x00B13650)
// leem estes campos; lemos direto para não chamar código do motor fora de hora.
constexpr size_t kLayers = 0xB8;          // DynArray<CAnimation> fifo[16]
constexpr size_t kLayerStride = 0x24;
constexpr int kNumLayers = 16;
constexpr size_t kAnimStride = 0x78;      // sizeof(CAnimation)
constexpr size_t kCharInstance = 0x3B4;   // CCharInstance* (this[0xED])
// CAnimation (nomes dos campos vindos de CAnimation::Serialize, 0x00A9C430)
constexpr size_t kAnim_Parametric = 0x00;  // != 0 em LMG/blend space
constexpr size_t kAnim_DirId0 = 0x04;      // int16 nAnimDirID0 (aim pose ligada à animação)
constexpr size_t kAnim_Id = 0x08;          // int16 nAnimID
constexpr size_t kAnim_Time = 0x10;        // float fAnimTime (0..1)
constexpr size_t kAnim_Weight = 0x18;      // float fTransitionWeight
constexpr size_t kAnim_Speed = 0x1C;       // float fCurrentPlaybackSpeed
// Modelo: CCharInstance+0x88 -> CCharacterModel; +0x11C -> ModelAnimationHeader[] (0x14 bytes,
// int16 id global em +8). Visto em 0x00B13750 e na serialização.
constexpr size_t kInst_Model = 0x88;
constexpr size_t kModel_AnimHeaders = 0x11C;
constexpr size_t kHeaderStride = 0x14;

template <class T>
bool readAt(const void* base, size_t off, T& out) {
    const auto* p = static_cast<const char*>(base) + off;
    if (!isReadable(p, sizeof(T))) return false;
    std::memcpy(&out, p, sizeof(T));
    return true;
}

// Quantidade de elementos de um DynArray do CryEngine (guardada antes dos dados).
int dynArrayCount(const void* data) {
    uint32_t raw = 0;
    if (!data || !readAt(data, static_cast<size_t>(-4), raw)) return -1;
    return static_cast<int>(raw & 0x7fffffff);
}

void* skeletonOfEntity(void* entity) {
    if (!looksLikeObject(entity)) return nullptr;
    using GetCharacter_t = void*(__thiscall*)(void*, int);
    void* character = (*reinterpret_cast<GetCharacter_t**>(entity))[vt::kEntity_GetCharacter / 4](entity, 0);
    if (!looksLikeObject(character)) return nullptr;
    using GetSkel_t = void*(__thiscall*)(void*);
    void* skel = (*reinterpret_cast<GetSkel_t**>(character))[vt::kCharacter_GetISkeletonAnim / 4](character);
    return looksLikeObject(skel) ? skel : nullptr;
}

void* skeletonOf(EntityId id) { return skeletonOfEntity(getEntity(id)); }

bool printable(const char* s, size_t maxLen) {
    if (!isReadable(s, 2)) return false;
    size_t n = 0;
    for (; n < maxLen; ++n) {
        if (!isReadable(s + n, 1)) return false;
        unsigned char c = s[n];
        if (!c) break;
        if (c < 0x20 || c > 0x7e) return false;
    }
    return n >= 2 && n < maxLen;
}

// Descobre em runtime em qual dword do ModelAnimationHeader está o nome (ponteiro de
// CryString). Feito uma vez, com várias entradas, e registrado no log.
int g_nameOffset = -2;  // -2 = ainda não testado, -1 = não encontrado

void detectNameOffset(const char* headers, int count) {
    int votes[kHeaderStride / 4] = {};
    for (int i = 0; i < count && i < 32; ++i) {
        for (size_t off = 0; off < kHeaderStride; off += 4) {
            const char* s = nullptr;
            if (readAt(headers + i * kHeaderStride, off, s) && printable(s, 256)) ++votes[off / 4];
        }
    }
    int best = -1;
    for (int k = 0; k < (int)(kHeaderStride / 4); ++k)
        if (votes[k] > 0 && (best < 0 || votes[k] > votes[best])) best = k;
    g_nameOffset = best < 0 ? -1 : best * 4;
    char dump[160];
    int n = 0;
    for (size_t off = 0; off < kHeaderStride && n < (int)sizeof(dump) - 12; off += 4) {
        uint32_t v = 0;
        readAt(headers, off, v);
        n += snprintf(dump + n, sizeof(dump) - n, " %08X", v);
    }
    log::info("anim_watch: %d cabecalhos; nome no offset %d (votos %d/%d/%d/%d/%d); entrada 0:%s", count,
              g_nameOffset, votes[0], votes[1], votes[2], votes[3], votes[4], dump);
}

// Catálogo de animações do modelo (uma vez), em sodcoop_anims.txt ao lado do exe:
// referência para escolher clipes a tocar no fantoche.
void dumpNames(const char* headers, int count) {
    char path[MAX_PATH];
    DWORD n = GetModuleFileNameA(nullptr, path, MAX_PATH);
    while (n > 0 && path[n - 1] != '\\') --n;
    snprintf(path + n, MAX_PATH - n, "sodcoop_anims.txt");
    FILE* f = fopen(path, "w");
    if (!f) return;
    int written = 0;
    for (int i = 0; i < count; ++i) {
        const char* s = nullptr;
        if (readAt(headers + i * kHeaderStride, g_nameOffset, s) && printable(s, 256)) {
            fprintf(f, "%d\t%s\n", i, s);
            ++written;
        }
    }
    fclose(f);
    log::info("anim_watch: %d nomes de animacao gravados em %s", written, path);
}

std::string animName(void* skel, int16_t animId) {
    char fallback[24];
    snprintf(fallback, sizeof(fallback), "#%d", animId);
    void* inst = nullptr;
    void* model = nullptr;
    const char* headers = nullptr;
    if (!readAt(skel, kCharInstance, inst) || !inst || !readAt(inst, kInst_Model, model) || !model ||
        !readAt(model, kModel_AnimHeaders, headers) || !headers)
        return fallback;
    const int count = dynArrayCount(headers);
    if (count <= 0 || count > 20000 || animId < 0 || animId >= count) return fallback;
    if (g_nameOffset == -2) {
        detectNameOffset(headers, count);
        if (g_nameOffset >= 0) dumpNames(headers, count);
    }
    if (g_nameOffset < 0) return fallback;
    const char* s = nullptr;
    if (!readAt(headers + animId * kHeaderStride, g_nameOffset, s) || !printable(s, 256)) return fallback;
    return s;
}

Action g_action, g_base, g_upper, g_aimPose;
float g_actionTime = 0, g_upperTime = 0;

bool startsWith(const std::string& s, const char* prefix) { return s.compare(0, strlen(prefix), prefix) == 0; }

// Locomoção, idles, viradas e camadas de cena ficam de fora: o fantoche já
// anda pelo próprio sistema de passos (remote_player).
bool isAction(const std::string& name, bool parametric) {
    if (parametric || name.empty() || name[0] == '#') return false;
    std::string lower = name;
    for (char& c : lower) c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
    if (lower.find("idle") != std::string::npos) return false;
    for (const char* p : {"test_hero_", "scenes_", "lipsync", "additive", "relaxed_", "weaponoffset"})
        if (startsWith(lower, p)) return false;
    return true;
}

std::string toLower(std::string s) {
    for (char& c : s) c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
    return s;
}

// Postura da camada 0 que o fantoche usa no lugar do idle/andar padrão.
bool isBase(const std::string& name, bool parametric) {
    if (name.empty() || name[0] == '#') return false;
    const std::string lower = toLower(name);
    if (parametric) return startsWith(lower, "_aim");
    if (lower == "relaxed_idle_nw" || lower == "test_hero_idle" || startsWith(lower, "scenes_")) return false;
    return lower.find("idle") != std::string::npos;
}

void setTrack(Action& track, float& lastTime, const std::string& name, float t, const char* label) {
    const bool same = name == track.name;
    // Mesma animação com o tempo voltando = começou de novo (ex.: dois golpes iguais seguidos).
    // Idles em loop voltam ao início a cada ciclo: nelas só conta a troca de nome.
    const bool looping = toLower(name).find("idle") != std::string::npos;
    if (!name.empty() && (!same || (!looping && t + 0.2f < lastTime))) {
        ++track.seq;
        log::info("%s: %s (seq %u)", label, name.c_str(), track.seq);
    }
    if (!same) snprintf(track.name, sizeof(track.name), "%s", name.c_str());
    lastTime = t;
}

void setBase(const std::string& name) {
    if (name == g_base.name) return;
    log::info("base: %s", name.empty() ? "(padrao)" : name.c_str());
    snprintf(g_base.name, sizeof(g_base.name), "%s", name.c_str());
}

}  // namespace

const Action& currentAction() { return g_action; }
const Action& currentBase() { return g_base; }
const Action& currentUpper() { return g_upper; }
const Action& currentAimPose() { return g_aimPose; }

void update(EntityId id, bool logChanges) {
    static std::string lastSig;
    static DWORD lastFail = 0;
    if (!id) {
        lastSig.clear();
        setTrack(g_action, g_actionTime, "", 0, "acao");
        setTrack(g_upper, g_upperTime, "", 0, "cima");
        setBase("");
        g_aimPose.name[0] = 0;
        return;
    }
    void* skel = skeletonOf(id);
    if (!skel) {
        if (GetTickCount() - lastFail > 10000) {
            lastFail = GetTickCount();
            log::warn("anim_watch: esqueleto do jogador indisponivel");
        }
        return;
    }
    // Assinatura = camadas e IDs ativos (sem tempo/peso, que mudam todo frame).
    std::string sig, detail, newestL0, newestL1;
    int16_t aimPoseId = -1;  // aim pose ligada a uma animação ativa da camada 0
    bool newestL0Parametric = false;
    float newestL0Time = 0, newestL1Time = 0;
    for (int layer = 0; layer < kNumLayers; ++layer) {
        const char* fifo = nullptr;
        if (!readAt(skel, kLayers + layer * kLayerStride, fifo) || !fifo) continue;
        const int n = dynArrayCount(fifo);
        if (n <= 0 || n > 16) continue;
        for (int i = 0; i < n; ++i) {
            const char* a = fifo + i * kAnimStride;
            uint32_t parametric = 0;
            int16_t animId = -1;
            float t = 0, w = 0, sp = 0;
            if (!readAt(a, kAnim_Parametric, parametric) || !readAt(a, kAnim_Id, animId)) continue;
            readAt(a, kAnim_Time, t);
            readAt(a, kAnim_Weight, w);
            readAt(a, kAnim_Speed, sp);
            char buf[64];
            snprintf(buf, sizeof(buf), "%d:%d ", layer, animId);
            sig += buf;
            const std::string name = animName(skel, animId);
            int16_t dir0 = -1;
            if (layer == 0 && readAt(a, kAnim_DirId0, dir0) && dir0 >= 0) aimPoseId = dir0;
            if (layer == 0) {  // a última entrada da fila é a mais nova
                newestL0 = name;
                newestL0Parametric = parametric != 0;
                newestL0Time = t;
            } else if (layer == 1 && !parametric) {
                newestL1 = name;
                newestL1Time = t;
            }
            char line[200];
            snprintf(line, sizeof(line), " [L%d #%d %s%s t=%.2f w=%.2f v=%.2f]", layer, animId, name.c_str(),
                     parametric ? " (LMG)" : "", t, w, sp);
            detail += line;
        }
    }
    const bool l0Action = isAction(newestL0, newestL0Parametric);
    setTrack(g_action, g_actionTime, l0Action ? newestL0 : std::string(), newestL0Time, "acao");
    setBase(isBase(newestL0, newestL0Parametric) ? newestL0 : std::string());
    snprintf(g_aimPose.name, sizeof(g_aimPose.name), "%s",
             aimPoseId >= 0 ? animName(skel, aimPoseId).c_str() : "");
    setTrack(g_upper, g_upperTime, newestL1[0] == '#' ? std::string() : newestL1, newestL1Time, "cima");
    if (!logChanges || sig == lastSig) return;
    lastSig = sig;
    log::info("anim:%s", detail.empty() ? " (nenhuma)" : detail.c_str());
}

}  // namespace sodcoop::anim_watch
