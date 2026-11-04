// Ponto de entrada do mod. O DllMain só cria a thread de inicialização;
// todo o resto roda fora do loader lock.
//
// Sequência (thread de inicialização):
//   1. config + log
//   2. confere o hash do exe (build suportado), senão o mod fica inerte
//   3. espera o SteamStub descriptografar a .text (prólogo conhecido + página executável)
//   4. instala hooks: jogador local (SetLocalActor) e frame (CCryAction::PreUpdate)
//   5. espera gEnv e as interfaces principais existirem; registra no log
// Depois disso, tudo acontece em onFrame(), na thread principal do jogo.
#include <windows.h>

#include <cctype>
#include <cmath>
#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

#include "core/build_check.h"
#include "core/clock.h"
#include "core/config.h"
#include "core/crash_log.h"
#include "core/focus_guard.h"
#include "core/log.h"
#include "core/window_keeper.h"
#include "game/anim_watch.h"
#include "game/base_sync.h"
#include "game/engine.h"
#include "game/frame.h"
#include "game/local_player.h"
#include "game/give_menu.h"
#include "game/zombie_activation.h"
#include "game/loot.h"
#include "game/puppet.h"
#include "game/remote_player.h"
#include "game/appearance.h"
#include "game/member_menu.h"
#include "game/vehicles.h"
#include "game/zombies.h"
#include "net/session.h"

namespace {

using namespace sodcoop;

Config g_cfg;

// Espera após o jogador local ser definido antes de criar o fantoche (nível assentar).
constexpr DWORD kPuppetSpawnDelayMs = 3000;

bool waitFor(bool (*cond)(), DWORD timeoutMs, DWORD stepMs) {
    for (DWORD waited = 0; waited < timeoutMs; waited += stepMs) {
        if (cond()) return true;
        Sleep(stepMs);
    }
    return cond();
}

bool coreInterfacesReady() {
    return engine::looksLikeObject(engine::gEnvField(engine::genv::kConsole)) &&
           engine::looksLikeObject(engine::gEnvField(engine::genv::kEntitySystem));
}

void logGEnvField(const char* name, size_t offset) {
    void* obj = engine::gEnvField(offset);
    bool ok = engine::looksLikeObject(obj);
    log::info("  gEnv+0x%02X %-14s = %p vtable=%p %s", (unsigned)offset, name, obj,
              ok ? *reinterpret_cast<void**>(obj) : nullptr, ok ? "OK" : "INVALIDO");
}

// Espelho local de teste (sem rede): fantoche copia o jogador local com deslocamento.
void updateMirror(const local_player::Snapshot& s, bool settled) {
    static bool puppetAlive = false;
    static int moveFailures = 0;
    if (!settled) {
        puppetAlive = false;
        return;
    }
    const DWORD now = GetTickCount();
    static DWORD retryAfter = 0;
    if (now < retryAfter) return;
    engine::Vec3 target{s.pos.x + g_cfg.mirrorOffsetX, s.pos.y + g_cfg.mirrorOffsetY, s.pos.z};
    if (!puppetAlive) {
        puppetAlive = puppet::spawn(g_cfg.puppetModel, target, g_cfg.anim.drivenMotion);
        if (!puppetAlive) retryAfter = now + kPuppetSpawnDelayMs;
        return;
    }
    if (!puppet::move(target, s.angles.z)) {
        if (++moveFailures <= 5) log::warn("puppet: move falhou; recriando o fantoche");
        puppetAlive = false;
        retryAfter = now + kPuppetSpawnDelayMs;
    }
}

// Calibração das animações: tempo (s) gasto em cada faixa de 0,5 m/s, a cada 10 s.
void logSpeedHistogram(float speed, float dt, DWORD now) {
    constexpr int kBins = 16;  // 0..8 m/s
    static float seconds[kBins] = {};
    static DWORD lastLog = now;
    if (dt > 0.0f && dt < 0.5f) {
        int bin = static_cast<int>(speed / 0.5f);
        seconds[bin < kBins ? bin : kBins - 1] += dt;
    }
    if (now - lastLog < 10000) return;
    lastLog = now;
    float moving = 0;
    for (int i = 1; i < kBins; ++i) moving += seconds[i];
    if (moving >= 0.5f) {
        char line[512];
        int n = snprintf(line, sizeof(line), "velocidade (s por faixa m/s):");
        for (int i = 0; i < kBins && n < (int)sizeof(line) - 24; ++i)
            if (seconds[i] >= 0.05f) n += snprintf(line + n, sizeof(line) - n, " %.1f-%.1f:%.1f", i * 0.5f,
                                                    i * 0.5f + 0.5f, seconds[i]);
        log::info("%s", line);
    }
    for (float& s : seconds) s = 0;
}

// Arma na mão do jogador local (modelo .cgf), lida pelo Lua a cada 0,5 s.
// O diagnóstico completo (item, pontos de fixação) vai para o log quando muda.
const std::string& localWeapon(bool inWorld, DWORD now) {
    static std::string weapon, lastDiag;
    static DWORD lastPoll = 0;
    if (!inWorld) {
        weapon.clear();
        return weapon;
    }
    if (now - lastPoll < 500) return weapon;
    lastPoll = now;
    std::string text;
    if (!puppet::readLuaText("SodCoop.LocalWeapon()", text)) {
        static int failures = 0;
        if (++failures <= 3) log::warn("arma: leitura pelo Lua falhou");
        return weapon;
    }
    const size_t bar = text.find('|');
    const std::string itemName = text.substr(0, bar);
    std::string diag = bar == std::string::npos ? "" : text.substr(bar + 1);
    // Modelo = primeiro slot da entidade do item com .cgf carregado.
    std::string model;
    if (void* item = itemName.empty() ? nullptr : engine::findEntityByName(itemName.c_str())) {
        const int slots = engine::getSlotCount(item);
        for (int i = 0; i < slots && i < 8; ++i) {
            const std::string path = engine::getSlotModelPath(item, i);
            if (path.empty()) continue;
            diag += " slot" + std::to_string(i) + "=" + path;
            if (model.empty()) model = path;
        }
        diag += " slots=" + std::to_string(slots);
    }
    if (diag != lastDiag) {
        lastDiag = diag;
        log::info("arma: %s -> modelo '%s'", diag.c_str(), model.c_str());
    }
    weapon = model;
    return weapon;
}

// Tiros do jogador local, contados pela munição do pente que cai (lida a cada 50 ms).
// g_gunShots: qualquer arma de fogo (vira dano nos zumbis do host; a animação de tiro não
// serve: a pistola não tem, e a arma automática não recomeça a animação a cada bala).
// Devolve só os de pistola (aim pose de pistola), que não têm animação: o outro lado toca
// uma no fantoche (fuzil e espingarda já têm a sua).
uint16_t g_gunShots = 0;
uint16_t localPistolShots(bool inWorld, DWORD now) {
    static uint16_t shots = 0;
    static std::string lastItem;
    static int lastAmmo = -1;
    static DWORD lastPoll = 0;
    if (!inWorld) {
        lastItem.clear();
        lastAmmo = -1;
        return shots;
    }
    if (now - lastPoll < 50) return shots;
    lastPoll = now;
    std::string text;
    if (!puppet::readLuaText("SodCoop.LocalAmmo()", text)) return shots;
    const size_t colon = text.rfind(':');
    const std::string item = colon == std::string::npos ? std::string() : text.substr(0, colon);
    const int ammo = colon == std::string::npos ? -1 : atoi(text.c_str() + colon + 1);
    const bool pistol = strcmp(anim_watch::currentAimPose().name, "AimPoses_pistol") == 0;
    if (item == lastItem && lastAmmo > 0 && ammo >= 0 && ammo < lastAmmo && lastAmmo - ammo <= 5)
    {
        g_gunShots = static_cast<uint16_t>(g_gunShots + (lastAmmo - ammo));
        static int logged = 0;
        if (logged++ < 30) log::info("arma: tiro %u de %s (pente %d -> %d)", g_gunShots, item.c_str(), lastAmmo, ammo);
    }
    if (pistol && item == lastItem && lastAmmo > 0 && ammo >= 0 && ammo < lastAmmo) {
        shots = static_cast<uint16_t>(shots + 1);
        static int logged = 0;
        if (logged++ < 20) log::info("arma: tiro de pistola %u (pente %d -> %d)", shots, lastAmmo, ammo);
    }
    lastItem = item;
    lastAmmo = ammo;
    return shots;
}

// Resoluções do menu de opções do jogo (FUN_015c4700, casos 0xF/0x10): três tabelas fixas
// de 5 larguras e 5 alturas por formato (0 = 4:3, 1 = 16:9, 2 = 16:10). A de 16:9 é
// 1280x720, 1366x768, 1600x900, 1920x1080, 3840x2160: pula de 1080p para 4K e não tem
// 2560x1440. O rótulo de cada opção é um texto traduzido à parte ("Resolution 3840x2160",
// em português "Resolução 3840 x 2160", no heap). A última opção de 16:9 vira 2560x1440:
// o número na tabela (na memória; o arquivo do jogo não muda) e o texto traduzido, que tem
// o mesmo tamanho. A tabela muda uma vez; o texto é procurado até ser achado.
void patchMenuResolutions(DWORD now) {
    static bool tableDone = false, textDone = false;
    static DWORD nextText = 0;
    static int textTries = 0;
    if (!tableDone) {
        tableDone = true;
        int* widths = reinterpret_cast<int*>(0x01898E5C);
        int* heights = reinterpret_cast<int*>(0x01898E70);
        DWORD old = 0;
        if (engine::isReadable(widths, 20) && engine::isReadable(heights, 20) && widths[4] == 3840 &&
            heights[4] == 2160 && VirtualProtect(widths, 0x28, PAGE_READWRITE, &old)) {
            widths[4] = 2560;
            heights[4] = 1440;
            VirtualProtect(widths, 0x28, old, &old);
            log::info("video: opcao 16:9 do menu 3840x2160 -> 2560x1440 (numeros)");
        } else {
            log::warn("video: tabela 16:9 do menu diferente da esperada; nao mexe");
            textDone = true;
        }
    }
    if (textDone || now < nextText || textTries >= 12) return;
    nextText = now + 5000;
    ++textTries;
    // Texto traduzido: "3840 x 2160" (com espaços; o nome interno "Resolution 3840x2160",
    // sem espaços, fica como está). Só memória privada de leitura e escrita (heap).
    static const char kOld[] = "3840 x 2160", kNew[] = "2560 x 1440";
    // Leitura e escrita por ReadProcessMemory/WriteProcessMemory: se outra thread do jogo
    // liberar a memória no meio, a chamada só falha (ler direto derrubaria o jogo).
    int replaced = 0;
    MEMORY_BASIC_INFORMATION mbi;
    static char chunk[1 << 20];
    const size_t len = sizeof(kOld) - 1;
    HANDLE self = GetCurrentProcess();
    for (uintptr_t a = 0x10000; a < 0x7FFF0000 && VirtualQuery(reinterpret_cast<void*>(a), &mbi, sizeof(mbi));
         a = reinterpret_cast<uintptr_t>(mbi.BaseAddress) + mbi.RegionSize) {
        if (mbi.State != MEM_COMMIT || mbi.Type != MEM_PRIVATE || mbi.Protect != PAGE_READWRITE) continue;
        const uintptr_t base = reinterpret_cast<uintptr_t>(mbi.BaseAddress);
        for (size_t off = 0; off < mbi.RegionSize; off += sizeof(chunk) - len) {
            const size_t n = mbi.RegionSize - off < sizeof(chunk) ? mbi.RegionSize - off : sizeof(chunk);
            SIZE_T got = 0;
            if (n < len || !ReadProcessMemory(self, reinterpret_cast<void*>(base + off), chunk, n, &got) || got != n)
                break;
            for (size_t k = 0; k + len <= n; ++k) {
                SIZE_T done = 0;
                if (chunk[k] == '3' && memcmp(chunk + k, kOld, len) == 0 &&
                    WriteProcessMemory(self, reinterpret_cast<void*>(base + off + k), kNew, len, &done) && done == len)
                    ++replaced;
            }
            if (n < sizeof(chunk)) break;
        }
    }
    if (replaced) {
        log::info("video: texto da opcao 16:9 \"3840 x 2160\" -> \"2560 x 1440\" (%d lugar(es))", replaced);
        textDone = true;
    } else if (textTries >= 12) {
        log::warn("video: texto \"3840 x 2160\" do menu nao achado");
    }
}

// Ferramenta de teste: se existir sodcoop_cmd.lua ao lado do exe, executa uma vez
// (thread principal) e apaga. Ex.: System.ExecuteCommand("i_giveitem Pistol_M9").
// Só com o jogador já no mundo: entidades criadas durante o carregamento do nível
// ficam "unremovable", e trocar o nome da entidade de texto dispara um aviso do motor.
void runCommandFile(DWORD now) {
    static DWORD lastCheck = 0;
    if (now - lastCheck < 1000) return;
    lastCheck = now;
    const std::wstring path = gameDirectory() + L"sodcoop_cmd.lua";
    HANDLE f = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (f == INVALID_HANDLE_VALUE) return;
    char buf[4096];
    DWORD got = 0;
    ReadFile(f, buf, sizeof(buf) - 1, &got, nullptr);
    CloseHandle(f);
    DeleteFileW(path.c_str());
    buf[got] = 0;
    puppet::ensureLuaHelpers();
    engine::executeLua("SodCoop = SodCoop or {}; SodCoop.cmdResult = nil", "sodcoop_cmd");
    const bool ok = engine::executeLua(buf, "sodcoop_cmd");
    log::info("cmd: sodcoop_cmd.lua (%lu bytes) -> %s: %s", got, ok ? "OK" : "FALHOU", buf);
    // O comando pode deixar um texto em SodCoop.cmdResult para o log.
    std::string result;
    if (puppet::readLuaText("SodCoop.cmdResult or ''", result) && !result.empty())
        log::info("cmd: resultado: %s", result.c_str());
    // "FACILITY_FINISH:<vaga>": termina na hora a construção da vaga (teste).
    if (result.compare(0, 16, "FACILITY_FINISH:") == 0) {
        base_sync::finishFacility(atoi(result.c_str() + 16));
        return;
    }
    // "FACILITY_SET:<vaga>:<variante>": troca a variante da instalação da vaga (teste de melhoria).
    if (result.compare(0, 13, "FACILITY_SET:") == 0) {
        const char* p = result.c_str() + 13;
        const int socket = atoi(p);
        const char* colon = strchr(p, ':');
        base_sync::finishFacility(socket, colon ? atoi(colon + 1) : -1);
        return;
    }
    // "ACTOR:<entityId>": registra a vtable do IActor da entidade (engenharia reversa).
    if (result.compare(0, 6, "ACTOR:") == 0) {
        const engine::EntityId aid = static_cast<engine::EntityId>(strtoul(result.c_str() + 6, nullptr, 0));
        void* actor = engine::getActor(frame::gameFramework(), aid);
        if (!actor) {
            log::warn("cmd: ator %u nao encontrado", aid);
            return;
        }
        void* const* vt = *reinterpret_cast<void* const* const*>(actor);
        char line[1024];
        int n = snprintf(line, sizeof(line), "cmd: ator %u = %p vtable %p:", aid, actor, vt);
        for (int i = 0; i < 48 && n < (int)sizeof(line) - 16; ++i)
            n += snprintf(line + n, sizeof(line) - n, " %02X:%p", i * 4, vt[i]);
        log::info("%s", line);
        using GetHealth_t = float(__thiscall*)(void*);
        log::info("cmd: ator %u GetHealth()=%.1f", aid,
                  reinterpret_cast<GetHealth_t>(vt[engine::vt::kActor_GetHealth / 4])(actor));
    }
}

// Cliente: ao entrar no mundo a mais de 30 m do jogador do host, vai para 2 m ao lado
// dele (uma vez por entrada no mundo). Com o mesmo save (launch.ps1 -SyncSave), os
// dois ficam no mesmo lugar do mesmo mundo.
void spawnNearHost(bool settled, const engine::Vec3& self, const net::RemoteState& remote, DWORD now) {
    static bool done = false;
    static DWORD settledSince = 0;
    static bool haveLast = false;
    static engine::Vec3 last{};
    if (!settled) {
        done = false;
        settledSince = 0;
        haveLast = false;
        return;
    }
    // Salto grande de um frame para o outro: o jogo pôs o jogador em outro lugar (ao morrer de
    // verdade, ele renasce no ponto inicial do mapa, visto em 2026-11-03). Leva de novo ao host.
    if (haveLast) {
        const float jx = self.x - last.x, jy = self.y - last.y;
        if (jx * jx + jy * jy > 80.0f * 80.0f) {
            log::info("spawn: jogador local saltou %.0f m (renasceu?); de volta ao host", std::sqrt(jx * jx + jy * jy));
            done = false;
            settledSince = now;
        }
    }
    last = self;
    haveLast = true;
    if (done) return;
    if (!settledSince) settledSince = now;
    if (now - settledSince > 60000) {  // host não apareceu no primeiro minuto: desiste
        done = true;
        return;
    }
    if (!remote.connected || !remote.state.hasPlayer || remote.state.dead) return;
    const float* h = remote.state.pos;
    const float dx = h[0] - self.x, dy = h[1] - self.y;
    done = true;
    const float dist = std::sqrt(dx * dx + dy * dy);
    if (dist <= 30.0f) return;
    char code[160];
    snprintf(code, sizeof(code), "g_localActor:SetWorldPos({ x = %.2f, y = %.2f, z = %.2f })", h[0] + 2.0f, h[1],
             h[2] + 0.5f);
    const bool ok = engine::executeLua(code, "sodcoop_spawn");
    log::info("spawn: cliente a %.0f m do host; levado para o lado dele (%.1f, %.1f, %.1f) -> %s", dist, h[0] + 2.0f,
              h[1], h[2] + 0.5f, ok ? "OK" : "FALHOU");
}

// [debug] start_pos / player_invisible: uma vez por entrada no mundo, 2 s depois de pronto.
void applyTestStart(bool settled, const engine::Vec3& self, DWORD now) {
    static bool done = false;
    static DWORD settledSince = 0;
    if (!settled) {
        done = false;
        settledSince = 0;
        return;
    }
    if (done) return;
    if (!settledSince) settledSince = now;
    if (now - settledSince < 2000) return;
    done = true;
    if (g_cfg.playerInvisible) {
        const bool ok = engine::executeLua("System.ExecuteCommand(\"zm_playerInvisible 1\")", "sodcoop_invisible");
        log::info("teste: zm_playerInvisible 1 -> %s", ok ? "OK" : "FALHOU");
    }
    if (g_cfg.godMode) {
        const bool ok = engine::executeLua(
            "local g = g_gameRules and g_gameRules.game; if g then g:SetInvulnerability(g_localActorId, true) end",
            "sodcoop_god");
        log::info("teste: SetInvulnerability -> %s", ok ? "OK" : "FALHOU");
    }
    if (g_cfg.startPosValid) {
        const float* p = g_cfg.startPos;
        const float dx = p[0] - self.x, dy = p[1] - self.y;
        if (dx * dx + dy * dy < 25.0f) return;  // já está lá (a menos de 5 m)
        char code[160];
        snprintf(code, sizeof(code), "g_localActor:SetWorldPos({ x = %.2f, y = %.2f, z = %.2f })", p[0], p[1],
                 p[2] + 0.3f);
        const bool ok = engine::executeLua(code, "sodcoop_startpos");
        log::info("teste: levado para start_pos (%.1f, %.1f, %.1f) -> %s", p[0], p[1], p[2], ok ? "OK" : "FALHOU");
    }
}

// Cliente: segue a hora do dia do host. Acerta quando a diferença passa de 0,02 h
// (~1 min de jogo); registra quantos acertos foram precisos a cada 10 s (se o
// relógio do jogo desfizer o acerto, o número sobe).
void followHostTime(bool settled, const net::RemoteState& remote, DWORD now) {
    static DWORD lastLog = 0;
    static int sets = 0;
    static float firstDiff = 0;
    if (!settled || !remote.connected || !remote.state.hasPlayer) return;
    const float host = remote.state.timeOfDay;
    float mine;
    if (host < 0 || host > 24 || !engine::getTimeOfDay(mine)) return;
    float diff = host - mine;
    if (diff > 12) diff -= 24;
    if (diff < -12) diff += 24;
    if (std::fabs(diff) > 0.02f && engine::setTimeOfDay(host)) {
        if (sets == 0) firstDiff = diff;
        ++sets;
    }
    if (now - lastLog >= 10000) {
        if (sets)
            log::info("hora: cliente acertado para a do host %d vez(es) em 10 s (host %.3f h; diferenca %.3f h)", sets,
                      host, firstDiff);
        lastLog = now;
        sets = 0;
    }
}

// Teclas de troca de sobrevivente (só com a janela deste jogo em primeiro plano, no
// mundo e vivo). Usa os comandos do jogo fm_prevMember / fm_nextMember; a aparência
// nova chega ao outro lado pelo pacote Look.
void memberSwitchKeys(bool settled, bool dead) {
    static bool prevDown = false, nextDown = false;
    auto pressed = [](int vk, bool& wasDown) {
        if (vk <= 0 || vk > 0xFE) return false;
        const bool down = (GetAsyncKeyState(vk) & 0x8000) != 0;
        const bool edge = down && !wasDown;
        wasDown = down;
        return edge;
    };
    // A tecla de "próximo" (F7) abre o menu de escolha (member_menu); aqui só a de "anterior".
    const bool prev = pressed(g_cfg.prevMemberKey, prevDown);
    const bool next = false;
    (void)nextDown;
    if (!prev && !next) return;
    DWORD pid = 0;
    GetWindowThreadProcessId(GetForegroundWindow(), &pid);
    if (pid != GetCurrentProcessId() || !settled || dead) return;
    const char* cmd = next ? "fm_nextMember" : "fm_prevMember";
    char code[64];
    snprintf(code, sizeof(code), "System.ExecuteCommand(\"%s\")", cmd);
    const bool ok = engine::executeLua(code, "sodcoop_member");
    log::info("sobrevivente: tecla -> %s -> %s", cmd, ok ? "OK" : "FALHOU");
}

// Jogador local morto (IActor::GetHealth <= 0). Registra a mudança no log.
bool localDead(engine::EntityId id) {
    static bool last = false;
    void* actor = engine::getActor(frame::gameFramework(), id);
    if (!actor) return last;
    using GetHealth_t = float(__thiscall*)(void*);
    void* const* vt = *reinterpret_cast<void* const* const*>(actor);
    const bool dead = reinterpret_cast<GetHealth_t>(vt[engine::vt::kActor_GetHealth / 4])(actor) <= 0.0f;
    if (dead != last) log::info("jogador local %s", dead ? "MORREU" : "vivo de novo");
    last = dead;
    return dead;
}

// Cliente: golpes (ação da camada 0 de arma branca) e tiros (munição do pente que cai,
// g_gunShots) do jogador local viram dano nos zumbis do host.
bool isMeleeAnim(const char* name) {
    std::string l = name;
    for (char& c : l) c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
    if (l.find("grab") != std::string::npos) return false;
    for (const char* k : {"bluntlight", "bluntheavy", "edgedlight", "edgedheavy", "knife_", "strike_", "unarmed_",
                          "heromelee"})
        if (l.compare(0, strlen(k), k) == 0) return true;
    return false;
}

void detectLocalAttacks(bool settled, const local_player::Snapshot& s, const net::PlayerState& mine) {
    static uint16_t lastAction = 0, lastShots = 0;
    static bool primed = false;
    const anim_watch::Action& action = anim_watch::currentAction();
    if (!settled) {
        primed = false;
        return;
    }
    if (!primed) {  // não conta o que já estava tocando ao entrar no mundo
        primed = true;
        lastAction = action.seq;
        lastShots = g_gunShots;
        return;
    }
    if (action.seq != lastAction) {
        lastAction = action.seq;
        if (isMeleeAnim(action.name))
            zombies::onLocalAttack(g_cfg.zombies, zombies::AttackKind::Melee, s.pos, s.angles.z, {});
    }
    // Um ataque por bala (no máximo 5 de uma vez: rajada entre duas leituras).
    for (int n = 0; lastShots != g_gunShots && n < 5; ++n) {
        lastShots = static_cast<uint16_t>(lastShots + 1);
        // Direção da mira: alvo do IK de mira (relativo à posição) menos a altura do peito.
        const engine::Vec3 dir{mine.aimOffset[0], mine.aimOffset[1], mine.aimOffset[2] - 1.4f};
        zombies::onLocalAttack(g_cfg.zombies, zombies::AttackKind::Shot, s.pos, s.angles.z, dir);
    }
    lastShots = g_gunShots;
}

// Thread principal do jogo, uma vez por frame.
void onFrame() {
    crash_log::ensureInstalled();  // o jogo pode trocar o filtro depois de iniciar
    static engine::EntityId lastId = 0;
    static DWORD idSince = 0, lastLog = 0, lastRemoteLog = 0;

    const DWORD now = GetTickCount();
    static bool cvarsApplied = false;
    if (!cvarsApplied && g_cfg.keepRunningUnfocused) {
        // Sem isto o motor reduz o frame rate da janela sem foco.
        bool ok = engine::executeLua("System.SetCVar(\"sys_SleepIfInactive\", 0)", "sodcoop_cvars");
        log::info("cvars: sys_SleepIfInactive=0 -> %s", ok ? "OK" : "FALHOU");
        cvarsApplied = true;
    }
    static bool fpsApplied = false;
    if (!fpsApplied && g_cfg.maxFps > 0) {
        // Duas instâncias sem limite (inclusive no menu) sobrecarregam a GPU; o PC
        // travou várias vezes com as duas cópias no menu (Kernel-Power 41).
        char code[96];
        snprintf(code, sizeof(code), "System.SetCVar(\"sys_MaxFPS\", %d)", g_cfg.maxFps);
        bool ok = engine::executeLua(code, "sodcoop_maxfps");
        log::info("cvars: sys_MaxFPS=%d -> %s", g_cfg.maxFps, ok ? "OK" : "FALHOU");
        fpsApplied = true;
    }
    patchMenuResolutions(now);
    static bool videoApplied = false;
    if (!videoApplied && g_cfg.window.enabled()) {
        // Resolução do jogo = área interna da janela fixa (borda do Windows: 16 x 39 px).
        // Sem isto o jogo volta à resolução salva (1920x1080, cobrindo a tela) e briga
        // com o window_keeper.
        char code[160];
        snprintf(code, sizeof(code),
                 "System.SetCVar(\"r_Fullscreen\", 0); System.SetCVar(\"r_Width\", %d); System.SetCVar(\"r_Height\", %d)",
                 g_cfg.window.width - 16, g_cfg.window.height - 39);
        bool ok = engine::executeLua(code, "sodcoop_video");
        log::info("cvars: r_Fullscreen=0 r_Width=%d r_Height=%d -> %s", g_cfg.window.width - 16,
                  g_cfg.window.height - 39, ok ? "OK" : "FALHOU");
        videoApplied = true;
    } else if (!g_cfg.window.enabled() && g_cfg.videoWidth > 0 && g_cfg.videoHeight > 0) {
        // [video]: resolução escolhida pelo jogador, inclusive as que o menu do jogo não lista.
        // O jogo reaplica a resolução salva depois (no mundo voltou a 1920x1080, 2026-11-03):
        // a cada 1 s confere os cvars e reaplica se mudaram (no máximo 30 vezes).
        static DWORD nextCheck = 0;
        static int applied = 0;
        if (now >= nextCheck && applied < 30) {
            nextCheck = now + 1000;
            std::string cur;
            puppet::readLuaText(
                "tostring(System.GetCVar(\"r_Width\")) .. 'x' .. tostring(System.GetCVar(\"r_Height\")) .. 'f' .. "
                "tostring(System.GetCVar(\"r_Fullscreen\"))",
                cur);
            char want[64];
            snprintf(want, sizeof(want), "%dx%df%d", g_cfg.videoWidth, g_cfg.videoHeight,
                     g_cfg.videoFullscreen >= 0 ? (g_cfg.videoFullscreen ? 1 : 0) : -1);
            const bool fsOk = g_cfg.videoFullscreen < 0 || (cur.size() >= 1 && cur.back() == want[strlen(want) - 1]);
            const std::string curSize = cur.substr(0, cur.find('f'));
            const std::string wantSize = std::string(want).substr(0, std::string(want).find('f'));
            if (curSize != wantSize || !fsOk) {
                char code[160];
                int n = snprintf(code, sizeof(code), "System.SetCVar(\"r_Width\", %d); System.SetCVar(\"r_Height\", %d)",
                                 g_cfg.videoWidth, g_cfg.videoHeight);
                if (g_cfg.videoFullscreen >= 0)
                    snprintf(code + n, sizeof(code) - n, "; System.SetCVar(\"r_Fullscreen\", %d)",
                             g_cfg.videoFullscreen ? 1 : 0);
                bool ok = engine::executeLua(code, "sodcoop_video");
                ++applied;
                log::info("cvars: [video] estava %s; aplicando %s (%d) -> %s", cur.c_str(), want, applied,
                          ok ? "OK" : "FALHOU");
            }
        }
        videoApplied = true;
    }
    engine::EntityId id = local_player::currentId();
    if (id != lastId) {
        lastId = id;
        idSince = now;
    }

    local_player::Snapshot s;
    const bool inWorld = id && local_player::sample(s);
    // Só considera o mundo "pronto" alguns segundos após o jogador local ser definido.
    const bool settled = inWorld && now - idSince >= kPuppetSpawnDelayMs;
    if (g_cfg.luaCommandFile && settled) runCommandFile(now);

    // Velocidade horizontal suavizada (constante de tempo kSpeedTau); zera em teleporte.
    // A posição do jogador não muda em todo frame, então a velocidade instantânea
    // é ruidosa; 0,35 s evita que a animação do outro lado fique trocando.
    constexpr float kSpeedTau = 0.35f;
    // Usa QueryPerformanceCounter: com GetTickCount (~15,6 ms) muitos frames tinham
    // dt=0 e a distância deles era perdida, subestimando a velocidade em até 70%.
    static engine::Vec3 prevPos{};
    static double prevSec = 0;
    static float speed = 0;
    if (inWorld) {
        const double nowSec = sodcoop::clock::seconds();
        const float dt = prevSec > 0 ? static_cast<float>(nowSec - prevSec) : 0.0f;
        const float dx = s.pos.x - prevPos.x, dy = s.pos.y - prevPos.y;
        const float dist = std::sqrt(dx * dx + dy * dy);
        if (prevSec == 0 || dist >= 20.0f) {
            speed = 0;
        } else if (dt > 0.0f) {
            const float k = 1.0f - std::exp(-dt / kSpeedTau);
            speed += (dist / dt - speed) * k;
        }
        prevPos = s.pos;
        prevSec = nowSec;
        logSpeedHistogram(speed, dt, now);
    } else {
        prevSec = 0;
        speed = 0;
    }

    anim_watch::update(settled ? id : 0, g_cfg.animWatch);

    net::PlayerState mine;
    mine.hasPlayer = settled;
    if (inWorld) {
        mine.pos[0] = s.pos.x;
        mine.pos[1] = s.pos.y;
        mine.pos[2] = s.pos.z;
        mine.yaw = s.angles.z;
        mine.speed = speed;
        const anim_watch::Action& action = anim_watch::currentAction();
        memcpy(mine.action, action.name, sizeof(mine.action));
        mine.actionSeq = action.seq;
        snprintf(mine.weapon, sizeof(mine.weapon), "%s", localWeapon(settled, now).c_str());
        memcpy(mine.base, anim_watch::currentBase().name, sizeof(mine.base));
        memcpy(mine.upper, anim_watch::currentUpper().name, sizeof(mine.upper));
        {
            // Tiro de pistola: vai como animação da parte de cima (o seq soma os dois
            // contadores, então muda a cada tiro e a cada animação nova da camada 1).
            static uint16_t lastShots = 0;
            static DWORD shotAt = 0;
            const uint16_t shots = localPistolShots(settled, now);
            if (shots != lastShots) {
                lastShots = shots;
                shotAt = now;
            }
            if (shotAt && now - shotAt < 350) snprintf(mine.upper, sizeof(mine.upper), "%s", "Gun_shoot_revolver");
            mine.upperSeq = static_cast<uint16_t>(anim_watch::currentUpper().seq + shots);
        }
        memcpy(mine.aimPose, anim_watch::currentAimPose().name, sizeof(mine.aimPose));
        mine.dead = settled && (localDead(id) || zombies::clientDowned());
        float hour;
        if (engine::getTimeOfDay(hour)) mine.timeOfDay = hour;
        vehicles::fillLocal(settled, mine);
        // IK de mira do jogador (o nome da animação não indica a mira: o jogo
        // liga o IK por cima de _AIM_* e também de relaxed_idle_*).
        bool aimOn = false;
        engine::Vec3 aimTarget{};
        float aimWeight = 0;
        if (engine::getAimIK(engine::getEntity(id), aimOn, aimTarget, aimWeight)) {
            mine.aiming = aimOn;
            mine.aimOffset[0] = aimTarget.x - s.pos.x;
            mine.aimOffset[1] = aimTarget.y - s.pos.y;
            mine.aimOffset[2] = aimTarget.z - s.pos.z;
            static bool lastAim = false;
            if (aimOn != lastAim) {
                lastAim = aimOn;
                log::info("mira: %s alvo-pos=(%.1f, %.1f, %.1f) peso=%.2f pose=%s", aimOn ? "LIGADA" : "desligada",
                          mine.aimOffset[0], mine.aimOffset[1], mine.aimOffset[2], aimWeight, mine.aimPose);
            }
        }
    }
    net::setLocalState(mine);
    memberSwitchKeys(settled, mine.dead);
    member_menu::update(g_cfg.nextMemberKey, settled, mine.dead);
    give_menu::update(g_cfg.giveItemKey, settled, mine.dead, net::getRemoteState().connected);
    loot::update(settled && net::getRemoteState().connected, g_cfg.net.role == net::Role::Host);
    base_sync::update(settled && net::getRemoteState().connected, g_cfg.net.role == net::Role::Host);

    const bool logNow = now - lastLog >= (DWORD)g_cfg.positionLogIntervalMs;
    if (logNow) {
        lastLog = now;
        if (inWorld)
            log::info("player id=%u pos=(%.2f, %.2f, %.2f) yaw=%.3f speed=%.2f", s.id, s.pos.x, s.pos.y,
                      s.pos.z, s.angles.z, speed);
    }

    if (g_cfg.net.role == net::Role::Off) {
        if (g_cfg.mirrorLocal) updateMirror(s, settled);
        return;
    }
    net::RemoteState remote = net::getRemoteState();
    if (logNow && remote.connected && now - lastRemoteLog >= 1000) {
        lastRemoteLog = now;
        log::info("remote id=%08X inWorld=%d pos=(%.2f, %.2f, %.2f) yaw=%.3f idade=%lums", remote.peerId,
                  remote.state.hasPlayer, remote.state.pos[0], remote.state.pos[1], remote.state.pos[2],
                  remote.state.yaw, now - remote.receivedAtMs);
    }
    // Dentro de carro, quem posiciona e anima o fantoche é vehicles (motorista ou passageiro);
    // remote_player só roda para criar o fantoche se ele ainda não existir.
    // Nada que leia o Lua (puppet::entity) antes de settled: a entidade de texto criada
    // durante o carregamento do nível fica "unremovable" e o motor reclama ao renomeá-la.
    const bool remoteInVehicle = vehicles::remoteSeated(remote);
    if (settled && !remoteInVehicle && vehicles::needsPuppetRespawn()) remote_player::respawn("saiu do carro");
    if (!settled || !remoteInVehicle || !puppet::entity())
        remote_player::update(remote, settled, appearance::puppetModel(g_cfg.puppetModel), g_cfg.anim);
    vehicles::updateRemote(remote, settled);
    appearance::updateLocal(settled, id);
    appearance::updatePuppet(settled && remote.connected && remote.state.hasPlayer);

    applyTestStart(settled, s.pos, now);
    // [debug] god_mode: vida cheia de novo sempre que baixar (só vivo: SetHealth(> 0) num ator
    // com vida 0 fecha o jogo).
    static DWORD nextGod = 0;
    if (g_cfg.godMode && settled && now >= nextGod) {
        nextGod = now + 200;
        engine::executeLua(
            "local a = g_localActor; if a and a.actor and not a:IsDead() then local h = a.actor:GetHealth(); "
            "local ok, m = pcall(a.actor.GetMaxHealth, a.actor); "
            "if ok and m and h > 0 and h < m then a.actor:SetHealth(m) end end",
            "sodcoop_godhp");
    }
    // Zumbis: o host manda os dele; o cliente desliga os próprios e mostra os do host.
    if (g_cfg.net.role == net::Role::Host) {
        const engine::Vec3 other{remote.state.pos[0], remote.state.pos[1], remote.state.pos[2]};
        // Jogador do cliente morto: os zumbis conduzidos pelo mod o soltam.
        const bool clientAlive = remote.connected && remote.state.hasPlayer && !remote.state.dead;
        zombies::updateHost(g_cfg.zombies, settled, s.pos, clientAlive, other);
        zombie_activation::setClient(g_cfg.zombies.clientActivation && settled && clientAlive, other);
        // Teste (sodcoop_cmd.lua: SodCoop.pullReq = n): leva n zumbis para perto do cliente.
        static DWORD nextPull = 0;
        if (settled && clientAlive && now >= nextPull) {
            nextPull = now + 500;
            std::string req;
            if (puppet::readLuaText("SodCoop.pullReq or 0", req) && atoi(req.c_str()) > 0) {
                engine::executeLua("SodCoop.pullReq = nil", "sodcoop_pull");
                zombie_activation::pullZombies(other, 12.0f, 200.0f, atoi(req.c_str()));
            }
        }
    } else {
        if (g_cfg.spawnNearHost) spawnNearHost(settled, s.pos, remote, now);
        if (g_cfg.syncTimeOfDay) followHostTime(settled, remote, now);
        zombies::updateClient(g_cfg.zombies, settled, s.pos);
        if (settled && remote.connected && remote.state.hasPlayer)
            zombies::clientRise({remote.state.pos[0], remote.state.pos[1], remote.state.pos[2]});
        detectLocalAttacks(settled, s, mine);
        zombies::updateGrenades(g_cfg.zombies, settled);
        zombies::updateFirePools(g_cfg.zombies, settled);
    }
}

DWORD WINAPI initThread(LPVOID) {
    g_cfg = loadConfig();
    std::wstring logPath = gameDirectory() + L"sodcoop_" +
                           std::wstring(g_cfg.instanceName.begin(), g_cfg.instanceName.end()) + L".log";
    log::open(logPath.c_str());
    log::info("==== sodcoop iniciando (instancia '%s', pid %lu) ====", g_cfg.instanceName.c_str(),
              GetCurrentProcessId());
    if (!g_cfg.enabled) {
        log::info("desativado em sodcoop.ini (enabled=0); jogo segue vanilla");
        return 0;
    }
    window_keeper::start(g_cfg.window);
    log::info("modelo do fantoche='%s' mirror_local=%d", g_cfg.puppetModel.c_str(), g_cfg.mirrorLocal);
    if (g_cfg.mirrorLocal && g_cfg.net.role != net::Role::Off)
        log::warn("mirror_local ignorado: so funciona com [net] role=off");
    if (!isSupportedBuild()) return 0;

    if (!waitFor(&engine::codeIsDecrypted, 120000, 10)) {
        log::error("codigo nao foi descriptografado em 120 s; abortando");
        return 0;
    }
    log::info("codigo descriptografado e executavel (prologo de 0x%08X confere)",
              (unsigned)engine::kSetLocalActor);

    // Hooks antes de o jogo carregar qualquer save.
    frame::addCallback(&onFrame);
    frame::setForceFocus(g_cfg.keepRunningUnfocused);
    // Sem isto o jogo abre o menu de pausa quando a janela perde o foco.
    if (g_cfg.keepRunningUnfocused) focus_guard::start();
    if (!local_player::installHook() || !frame::installHook()) return 0;
    if (g_cfg.net.role == net::Role::Host && g_cfg.zombies.clientActivation) zombie_activation::installHook(g_cfg.zombies.clientPointType, g_cfg.zombies.clientPointIndex);

    if (!waitFor(&coreInterfacesReady, 300000, 100)) {
        log::error("gEnv/interfaces nao ficaram prontas em 300 s");
        return 0;
    }
    log::info("gEnv = %p (lido de 0x%08X)", engine::gEnv(), (unsigned)engine::kGEnvPtr);
    logGEnvField("pConsole", engine::genv::kConsole);
    logGEnvField("pScriptSystem", engine::genv::kScriptSystem);
    logGEnvField("pPhysicalWorld", engine::genv::kPhysicalWorld);
    logGEnvField("pEntitySystem", engine::genv::kEntitySystem);
    logGEnvField("pLog", engine::genv::kLog);

    if (!net::start(g_cfg.net)) log::error("net: falha ao iniciar a sessao; seguindo sem rede");
    return 0;
}

}  // namespace

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(inst);
        HANDLE t = CreateThread(nullptr, 0, &initThread, nullptr, 0, nullptr);
        if (t) CloseHandle(t);
    }
    return TRUE;
}
