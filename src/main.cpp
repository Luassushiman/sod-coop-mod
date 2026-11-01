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

#include <cmath>
#include <cstring>
#include <string>

#include "core/build_check.h"
#include "core/clock.h"
#include "core/config.h"
#include "core/log.h"
#include "core/window_keeper.h"
#include "game/anim_watch.h"
#include "game/engine.h"
#include "game/frame.h"
#include "game/local_player.h"
#include "game/puppet.h"
#include "game/remote_player.h"
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
    engine::EntityId id = local_player::currentId();
    if (id != lastId) {
        lastId = id;
        idSince = now;
    }

    local_player::Snapshot s;
    const bool inWorld = id && local_player::sample(s);
    // Só considera o mundo "pronto" alguns segundos após o jogador local ser definido.
    const bool settled = inWorld && now - idSince >= kPuppetSpawnDelayMs;

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
