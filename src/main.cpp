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
#include <cstring>
#include <string>

#include "core/build_check.h"
#include "core/clock.h"
#include "core/config.h"
#include "core/focus_guard.h"
#include "core/log.h"
#include "core/window_keeper.h"
#include "game/anim_watch.h"
#include "game/engine.h"
#include "game/frame.h"
#include "game/local_player.h"
#include "game/puppet.h"
#include "game/remote_player.h"
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

// Cliente: golpes (ação da camada 0 de arma branca) e tiros (camada 1 "Gun_shoot*")
// do jogador local viram dano nos zumbis do host.
bool isMeleeAnim(const char* name) {
    std::string l = name;
    for (char& c : l) c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
    if (l.find("grab") != std::string::npos) return false;
    for (const char* k : {"bluntlight", "bluntheavy", "edgedlight", "edgedheavy", "knife_", "strike_", "unarmed_",
                          "heromelee"})
        if (l.compare(0, strlen(k), k) == 0) return true;
    return false;
}

bool isShotAnim(const char* name) {
    return strncmp(name, "Gun_shoot", 9) == 0 || strncmp(name, "Shoot_", 6) == 0;
}

void detectLocalAttacks(bool settled, const local_player::Snapshot& s, const net::PlayerState& mine) {
    static uint16_t lastAction = 0, lastUpper = 0;
    static bool primed = false;
    const anim_watch::Action& action = anim_watch::currentAction();
    const anim_watch::Action& upper = anim_watch::currentUpper();
    if (!settled) {
        primed = false;
        return;
    }
    if (!primed) {  // não conta o que já estava tocando ao entrar no mundo
        primed = true;
        lastAction = action.seq;
        lastUpper = upper.seq;
        return;
    }
    if (action.seq != lastAction) {
        lastAction = action.seq;
        if (isMeleeAnim(action.name))
            zombies::onLocalAttack(g_cfg.zombies, zombies::AttackKind::Melee, s.pos, s.angles.z, {});
    }
    if (upper.seq != lastUpper) {
        lastUpper = upper.seq;
        if (isShotAnim(upper.name)) {
            // Direção da mira: alvo do IK de mira (relativo à posição) menos a altura do peito.
            const engine::Vec3 dir{mine.aimOffset[0], mine.aimOffset[1], mine.aimOffset[2] - 1.4f};
            zombies::onLocalAttack(g_cfg.zombies, zombies::AttackKind::Shot, s.pos, s.angles.z, dir);
        }
    }
}

// Thread principal do jogo, uma vez por frame.
void onFrame() {
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
        mine.upperSeq = anim_watch::currentUpper().seq;
        memcpy(mine.aimPose, anim_watch::currentAimPose().name, sizeof(mine.aimPose));
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
    remote_player::update(remote, settled, g_cfg.puppetModel, g_cfg.anim);

    // Zumbis: o host manda os dele; o cliente desliga os próprios e mostra os do host.
    if (g_cfg.net.role == net::Role::Host) {
        const engine::Vec3 other{remote.state.pos[0], remote.state.pos[1], remote.state.pos[2]};
        zombies::updateHost(g_cfg.zombies, settled, s.pos, remote.connected && remote.state.hasPlayer, other);
    } else {
        zombies::updateClient(g_cfg.zombies, settled, s.pos);
        detectLocalAttacks(settled, s, mine);
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
