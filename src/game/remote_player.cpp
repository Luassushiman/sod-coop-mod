#include "game/remote_player.h"

#include <windows.h>
#include <cctype>
#include <cmath>
#include <cstring>

#include "core/clock.h"
#include "core/log.h"
#include "game/puppet.h"

namespace sodcoop::remote_player {
namespace {

// --- Interpolação de snapshots ---------------------------------------------
// O fantoche é desenhado kRenderDelay segundos "no passado" do remetente,
// interpolando entre os dois snapshots que cercam esse instante. Assim a
// velocidade na tela é constante (sem os pulsos de seguir o último pacote).
constexpr double kRenderDelay = 0.10;     // s (~2 pacotes a 20 Hz)
constexpr double kMaxExtrapolate = 0.15;  // s além do último snapshot
constexpr float kTeleportDistance = 10.0f;
constexpr int kBufferSize = 32;
constexpr float kPi = 3.14159265f;

struct Snapshot {
    double t;  // relógio do remetente (s)
    engine::Vec3 pos;
    float yaw;
    float speed;
    char action[48];  // ação do remetente neste instante (vazio = só locomoção)
    uint16_t actionSeq;
    char base[48];    // postura da camada 0 (idle com arma, mira)
    char upper[48];   // camada 1 (tiro, recarga, segurar arma)
    uint16_t upperSeq;
};

Snapshot g_buf[kBufferSize];
int g_count = 0;          // snapshots válidos, ordenados por t (mais antigo primeiro)
uint32_t g_lastSeq = 0;
double g_offset = 0;      // relógio local - relógio do remetente (s), estimado
bool g_offsetValid = false;

float wrapAngle(float a) {
    while (a > kPi) a -= 2 * kPi;
    while (a < -kPi) a += 2 * kPi;
    return a;
}

void resetBuffer() {
    g_count = 0;
    g_lastSeq = 0;
    g_offsetValid = false;
}

void pushSnapshot(const net::RemoteState& r) {
    const double senderT = r.senderTimeMs / 1000.0;
    // Offset de relógio: segue rápido para baixo (pacote que chegou mais
    // depressa = menos atraso de rede) e sobe devagar (deriva de relógio).
    const double o = r.receivedAtSec - senderT;
    if (!g_offsetValid || o < g_offset) g_offset = o;
    else g_offset += (o - g_offset) * 0.01;
    g_offsetValid = true;

    if (g_count > 0 && senderT <= g_buf[g_count - 1].t) return;  // fora de ordem
    if (g_count == kBufferSize) {
        for (int i = 1; i < kBufferSize; ++i) g_buf[i - 1] = g_buf[i];
        --g_count;
    }
    Snapshot& s = g_buf[g_count++];
    s = {senderT, {r.state.pos[0], r.state.pos[1], r.state.pos[2]}, r.state.yaw, r.state.speed, {}, r.state.actionSeq};
    memcpy(s.action, r.state.action, sizeof(s.action));
    s.action[sizeof(s.action) - 1] = 0;
    memcpy(s.base, r.state.base, sizeof(s.base));
    s.base[sizeof(s.base) - 1] = 0;
    memcpy(s.upper, r.state.upper, sizeof(s.upper));
    s.upper[sizeof(s.upper) - 1] = 0;
    s.upperSeq = r.state.upperSeq;
}

// Diagnóstico da última amostragem (para registrar saltos).
const char* g_sampleMode = "";
double g_sampleAhead = 0;  // s além do último snapshot (extrapolação)

// Estado interpolado no instante renderT (relógio do remetente).
bool sample(double renderT, Snapshot& out) {
    g_sampleMode = "interp";
    g_sampleAhead = 0;
    if (g_count == 0) return false;
    if (renderT <= g_buf[0].t) {
        g_sampleMode = "antes-do-buffer";
        out = g_buf[0];
        return true;
    }
    for (int i = 0; i + 1 < g_count; ++i) {
        const Snapshot& a = g_buf[i];
        const Snapshot& b = g_buf[i + 1];
        if (renderT > b.t) continue;
        const float dx = b.pos.x - a.pos.x, dy = b.pos.y - a.pos.y, dz = b.pos.z - a.pos.z;
        if (std::sqrt(dx * dx + dy * dy + dz * dz) > kTeleportDistance) {
            out = b;  // teleporte (respawn, carregamento): não interpola
            return true;
        }
        const float k = static_cast<float>((renderT - a.t) / (b.t - a.t));
        out = a;  // ação: vale a do snapshot anterior (eventos discretos não se interpolam)
        out.t = renderT;
        out.pos = {a.pos.x + dx * k, a.pos.y + dy * k, a.pos.z + dz * k};
        out.yaw = wrapAngle(a.yaw + wrapAngle(b.yaw - a.yaw) * k);
        out.speed = a.speed + (b.speed - a.speed) * k;
        return true;
    }
    // Além do último snapshot: extrapola pouco, com a velocidade dos dois últimos.
    const Snapshot& last = g_buf[g_count - 1];
    out = last;
    g_sampleMode = "extrapolado";
    g_sampleAhead = renderT - last.t;
    if (g_count >= 2) {
        const Snapshot& prev = g_buf[g_count - 2];
        const double span = last.t - prev.t;
        const double ahead = std::fmin(renderT - last.t, kMaxExtrapolate);
        if (span > 0 && ahead > 0) {
            const float k = static_cast<float>(ahead / span);
            out.pos.x += (last.pos.x - prev.pos.x) * k;
            out.pos.y += (last.pos.y - prev.pos.y) * k;
            out.pos.z += (last.pos.z - prev.pos.z) * k;
        }
    }
    return true;
}

// --- Escolha da animação ---------------------------------------------------
enum class Gait { None = -1, Idle, Walk, Run, Sprint };

constexpr double kMinGaitDwell = 0.4;  // s: tempo mínimo numa animação antes de trocar

// Escolhe o passo pela velocidade, com histerese em torno de cada limiar
// (para subir de faixa precisa passar limiar+h; para descer, limiar-h).
Gait classify(float speed, Gait current, const AnimConfig& a) {
    const float thresholds[3] = {a.walkMin, a.runMin, a.sprintMin};
    int level = current == Gait::None ? -1 : static_cast<int>(current);
    if (level < 0) {
        level = 0;
        for (int i = 0; i < 3; ++i)
            if (speed >= thresholds[i]) level = i + 1;
        return static_cast<Gait>(level);
    }
    while (level < 3 && speed >= thresholds[level] + a.hysteresis) ++level;
    while (level > 0 && speed < thresholds[level - 1] - a.hysteresis) --level;
    return static_cast<Gait>(level);
}

const std::string& clipFor(Gait g, const AnimConfig& a) {
    switch (g) {
        case Gait::Walk: return a.walk;
        case Gait::Run: return a.run;
        case Gait::Sprint: return a.sprint;
        default: return a.idle;
    }
}

float nominalSpeed(Gait g, const AnimConfig& a) {
    switch (g) {
        case Gait::Walk: return a.walkSpeed;
        case Gait::Run: return a.runSpeed;
        case Gait::Sprint: return a.sprintSpeed;
        default: return 0.0f;
    }
}

// Clipe de mira parada (rifle no ombro), nome do .chrparams (regra "Story_*").
constexpr const char* kStandAimClip = "Story_ar02_Quarantine_Approach_Soldiers_StandAim";
// Mesma cena, soldado mirando para baixo (não há versão para cima).
constexpr const char* kStandAimDownClip = "Story_ar02_Quarantine_Approach_Soldier_StandAim_Down";
// Pistola: não há pose de mira pronta. Idle com pistola no corpo e o tiro de
// revólver parado no início (braço estendido) na camada 1.
constexpr const char* kPistolIdleClip = "relaxed_idle_pistol";
constexpr const char* kPistolAimPose = "Gun_shoot_revolver";
constexpr float kPistolAimPoseTime = 0.15f;
// Inclinação da mira (graus) a partir da qual usa a pose para baixo, com histerese.
constexpr float kAimDownEnterDeg = -15.0f, kAimDownExitDeg = -10.0f;
constexpr float kChestHeight = 1.5f;  // m acima dos pés, origem da mira
constexpr const char* kWalkAimClip = "Test_hero_walk_forward_aim";
constexpr const char* kRunAimClip = "Test_hero_run_forward_aim";

// Aim poses de arma longa (o clipe acima é com rifle; pistola fica de fora).
bool isLongGunAimPose(const char* aimPose) {
    for (const char* p : {"AimPoses_shotgun", "AimPoses_rifle", "AimPoses_assault", "AimPoses_smg"})
        if (strcmp(aimPose, p) == 0) return true;
    return false;
}

const char* gaitName(Gait g) {
    static const char* names[] = {"idle", "walk", "run", "sprint"};
    return g == Gait::None ? "none" : names[static_cast<int>(g)];
}

// --- Estado do fantoche ----------------------------------------------------
bool g_alive = false;
DWORD g_retryAfterMs = 0;
Gait g_gait = Gait::None;
double g_gaitSince = 0;
float g_rate = 1.0f;
DWORD g_lastRateMs = 0;
uint16_t g_actionSeq = 0;  // última ação vista do remetente
bool g_inAction = false;   // fantoche tocando uma ação (passos suspensos)
std::string g_clip;        // animação em loop na camada 0 (passo ou postura base)
uint16_t g_upperSeq = 0;   // última animação da camada 1 vista do remetente
bool g_upperOn = false;    // camada 1 tocando no fantoche
bool g_aimDown = false;    // mira inclinada para baixo (pose "down")
bool g_pistolPose = false; // pose de mira de pistola aplicada (camada 1)
std::string g_weapon;      // arma presa no fantoche
DWORD g_weaponRetryMs = 0;

unsigned g_spawnCount = 0;
bool g_ragdoll = false;  // fantoche virou corpo mole (outro jogador morto)
int g_ragdollStep = 0;   // próximo passo de puppet::ragdoll
DWORD g_ragdollAtMs = 0;

void despawn(const char* reason) {
    if (!g_alive) return;
    log::info("remote_player: removendo fantoche (%s)", reason);
    puppet::remove();
    g_alive = false;
}

// --- Diagnóstico de saltos ---------------------------------------------------
engine::Vec3 g_lastSetPos{};
bool g_haveLastSet = false;
engine::Vec3 g_prevDelta{};
int g_jumpLogs = 0, g_driftLogs = 0;

// Deslocamento da raiz medido pelo Lua (canal da sonda): por segundo, máximo,
// média e quantos "resets" (variação > 0,3 m entre frames = ciclo reiniciando).
void measureRootOffset(double nowSec, float speed) {
    static double windowStart = 0;
    static float maxOff = 0, sumOff = 0, prevDx = 0, prevDy = 0, sumSpeed = 0;
    static int frames = 0, resets = 0, noBone = 0;
    static bool havePrev = false;
    engine::Vec3 p;
    if (!puppet::readProbe(p) || p.z < 4999.0f) return;
    if (p.z < 5000.5f) ++noBone;  // z=5000: GetBonePos falhou
    const float off = std::sqrt(p.x * p.x + p.y * p.y);
    if (havePrev) {
        const float jx = p.x - prevDx, jy = p.y - prevDy;
        if (jx * jx + jy * jy > 0.09f) ++resets;
    }
    prevDx = p.x;
    prevDy = p.y;
    havePrev = true;
    maxOff = off > maxOff ? off : maxOff;
    sumOff += off;
    sumSpeed += speed;
    ++frames;
    if (windowStart == 0) windowStart = nowSec;
    if (nowSec - windowStart < 1.0) return;
    if (sumSpeed / frames > 0.3f || maxOff > 0.05f)
        log::info("diag raiz: desloc max=%.2f m media=%.2f m resets=%d frames=%d vel_media=%.2f sem_osso=%d",
                  maxOff, sumOff / frames, resets, frames, sumSpeed / frames, noBone);
    windowStart = nowSec;
    maxOff = sumOff = sumSpeed = 0;
    frames = resets = noBone = 0;
}

void diagnose(const Snapshot& s, bool rootCompensation) {
    if (!g_haveLastSet) return;
    // 1) Algo além do mod moveu a entidade desde o último frame?
    //    (Com compensação de raiz a própria compensação desloca a entidade.)
    engine::Vec3 cur;
    if (!rootCompensation && puppet::currentPosition(cur)) {
        const float ex = cur.x - g_lastSetPos.x, ey = cur.y - g_lastSetPos.y, ez = cur.z - g_lastSetPos.z;
        const float drift = std::sqrt(ex * ex + ey * ey + ez * ez);
        if (drift > 0.05f && g_driftLogs < 20) {
            ++g_driftLogs;
            log::warn("diag: entidade do fantoche moveu %.2f m sozinha entre frames (%.2f,%.2f,%.2f)", drift, ex,
                      ey, ez);
        }
    }
    // 2) O ponto desenhado inverteu o sentido (salto para trás)?
    const engine::Vec3 d{s.pos.x - g_lastSetPos.x, s.pos.y - g_lastSetPos.y, s.pos.z - g_lastSetPos.z};
    const float len = std::sqrt(d.x * d.x + d.y * d.y);
    const float dot = d.x * g_prevDelta.x + d.y * g_prevDelta.y;
    if (len > 0.15f && dot < 0 && g_jumpLogs < 40) {
        ++g_jumpLogs;
        const Snapshot& last = g_buf[g_count - 1];
        log::warn("diag: salto para tras de %.2f m (modo=%s alem_do_ultimo=%.0fms buffer=%d span=%.0fms "
                  "offset=%.1fms)",
                  len, g_sampleMode, g_sampleAhead * 1000, g_count, (last.t - g_buf[0].t) * 1000,
                  g_offset * 1000);
    }
    if (len > 0.001f) g_prevDelta = d;
}

}  // namespace

void respawn(const char* reason) { despawn(reason); }

unsigned spawnCount() { return g_spawnCount; }

void update(const net::RemoteState& remote, bool localInWorld, const std::string& model,
            const AnimConfig& anim) {
    const DWORD now = GetTickCount();
    const double nowSec = clock::seconds();

    if (!remote.connected) resetBuffer();
    else if (remote.sequence != g_lastSeq) {
        g_lastSeq = remote.sequence;
        if (remote.state.hasPlayer) pushSnapshot(remote);
        else g_count = 0;  // saiu do mundo: descarta o histórico
    }

    if (!localInWorld) {  // nível descarregado: a entidade já não existe
        g_alive = false;
        return;
    }
    if (!remote.connected || !remote.state.hasPlayer) {
        despawn(!remote.connected ? "desconectado" : "outro jogador fora do mundo");
        return;
    }

    Snapshot s;
    if (!g_offsetValid || !sample(nowSec - g_offset - kRenderDelay, s)) return;

    if (!g_alive) {
        if (now < g_retryAfterMs) return;
        g_alive = puppet::spawn(model, s.pos, anim.drivenMotion);
        g_gait = Gait::None;  // força tocar a animação no fantoche novo
        g_inAction = false;
        g_actionSeq = s.actionSeq;  // não repete uma ação que já passou
        g_weapon.clear();
        g_clip.clear();
        g_upperOn = false;
        g_aimDown = g_pistolPose = false;
        g_upperSeq = static_cast<uint16_t>(s.upperSeq - 1);  // toca a camada 1 atual
        g_ragdoll = false;
        if (!g_alive) g_retryAfterMs = now + 3000;
        else {
            ++g_spawnCount;
            log::info("remote_player: fantoche do jogador %08X criado (%s)", remote.peerId, model.c_str());
        }
        return;
    }

    // Outro jogador morto: lá o corpo cai por física, sem animação de morte. O fantoche vira
    // corpo mole e para de ser movido; quando o outro jogador volta, é recriado em pé.
    if (remote.state.dead) {
        if (!g_ragdoll) {
            g_ragdoll = true;
            g_ragdollStep = 0;
            g_ragdollAtMs = now;
        }
        // 0 = física; 300 ms depois, o ragdoll; 1 s depois, de novo (garante a queda).
        if (g_ragdollStep <= 2 && now >= g_ragdollAtMs) {
            const bool ok = puppet::ragdoll(g_ragdollStep == 0 ? 0 : 1);
            log::info("remote_player: outro jogador morto; ragdoll do fantoche passo %d -> %s", g_ragdollStep,
                      ok ? "OK" : "FALHOU");
            g_ragdollAtMs = now + (g_ragdollStep == 0 ? 300 : 700);
            ++g_ragdollStep;
        }
        return;
    }
    if (g_ragdoll) {
        despawn("outro jogador voltou a viver");
        return;
    }

    // Arma: troca quando o outro lado troca (o estado mais recente, sem atraso).
    if (remote.state.weapon != g_weapon && now >= g_weaponRetryMs) {
        if (puppet::setWeapon(remote.state.weapon)) g_weapon = remote.state.weapon;
        else g_weaponRetryMs = now + 5000;
    }

    // Ações (golpes, pulos...): tocam uma vez por cima da locomoção; ao terminar,
    // o passo atual é tocado de novo.
    if (s.action[0] && s.actionSeq != g_actionSeq) {
        log::info("remote_player: acao %s (seq %u)", s.action, s.actionSeq);
        // O multiplicador de ritmo da camada vale para qualquer animação: volta a 1.
        if (g_rate != 1.0f && puppet::setAnimationRate(1.0f)) g_rate = 1.0f;
        g_inAction = puppet::playAnimation(s.action, 1.0f, false, 0.1f);
    } else if (!s.action[0] && g_inAction) {
        g_inAction = false;
        g_clip.clear();  // volta a tocar o passo/postura
    }
    g_actionSeq = s.actionSeq;

    Gait gait = g_inAction ? g_gait : classify(s.speed, g_gait, anim);
    if (gait != g_gait && (g_gait == Gait::None || nowSec - g_gaitSince >= kMinGaitDwell)) {
        log::info("remote_player: passo %s -> %s (%.2f m/s)", gaitName(g_gait), gaitName(gait), s.speed);
        g_gait = gait;
        g_gaitSince = nowSec;
    }
    // Postura base do remetente: mira (_AIM_*) vale parado ou andando; idle com
    // arma só substitui o idle. Sem postura, o passo escolhido pela velocidade.
    const bool aiming = strncmp(s.base, "_AIM", 4) == 0;
    const bool useBase = aiming || (s.base[0] && g_gait == Gait::Idle);
    // Mira parada com arma longa: no jogador a arma sobe pelo IK de aim poses do
    // motor, que não roda num AnimObject (peso fica 0; ver RE_ENDERECOS 4c). Usa um
    // clipe de corpo inteiro do próprio jogo com o rifle no ombro (cena da quarentena).
    const bool longGunAim = remote.state.aiming && isLongGunAimPose(remote.state.aimPose);
    const bool standAim = longGunAim && g_gait == Gait::Idle;
    // Inclinação da mira pelo alvo do IK do remetente (relativo aos pés dele).
    {
        const float* o = remote.state.aimOffset;
        const float pitch = std::atan2(o[2] - kChestHeight, std::sqrt(o[0] * o[0] + o[1] * o[1])) * 57.2958f;
        const bool down = remote.state.aiming && (g_aimDown ? pitch < kAimDownExitDeg : pitch < kAimDownEnterDeg);
        if (down != g_aimDown) log::info("remote_player: mira %s (%.0f graus)", down ? "para baixo" : "reta", pitch);
        g_aimDown = down;
    }
    const bool pistolAim = remote.state.aiming && strcmp(remote.state.aimPose, "AimPoses_pistol") == 0;
    // Andando/correndo mirando: clipes "para frente com mira" do jogo (os mesmos
    // que o blend space _AIM_* usa), sem a direção lateral.
    const bool moveAim = longGunAim && g_gait != Gait::Idle && g_gait != Gait::None;
    if (!g_inAction) {
        const std::string clip = standAim  ? std::string(g_aimDown ? kStandAimDownClip : kStandAimClip)
                                 : (pistolAim && g_gait == Gait::Idle) ? std::string(kPistolIdleClip)
                                 : moveAim ? std::string(g_gait == Gait::Walk ? kWalkAimClip : kRunAimClip)
                                 : useBase ? std::string(s.base)
                                           : clipFor(g_gait, anim);
        if (clip != g_clip) {
            if (useBase || standAim || moveAim) log::info("remote_player: postura %s", clip.c_str());
            if (g_rate != 1.0f && puppet::setAnimationRate(1.0f)) g_rate = 1.0f;
            puppet::playAnimation(clip, 1.0f);  // falha já vai para o log
            g_clip = clip;  // não repete a cada frame se falhar
        }
    }

    // Pistola mirando: braço estendido (pose fixa na camada 1), parado ou andando.
    if (pistolAim != g_pistolPose) {
        if (pistolAim) g_pistolPose = puppet::holdPose(kPistolAimPose, kPistolAimPoseTime, 1);
        else {
            puppet::stopLayer(1);
            g_pistolPose = false;
        }
    }

    // Parte de cima do remetente (tiro, recarga, segurar arma) vai na camada 2 do
    // fantoche: idles em loop, o resto uma vez.
    if (s.upperSeq != g_upperSeq && s.upper[0]) {
        std::string lower = s.upper;
        for (char& c : lower) c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
        const bool loop = lower.find("idle") != std::string::npos;
        log::info("remote_player: parte de cima %s (seq %u%s)", s.upper, s.upperSeq, loop ? ", loop" : "");
        g_upperOn = puppet::playAnimation(s.upper, 1.0f, loop, 0.15f, 2);
    } else if (!s.upper[0] && g_upperOn) {
        puppet::stopLayer(2);
        g_upperOn = false;
    }
    g_upperSeq = s.upperSeq;

    // Ritmo dos passos proporcional à velocidade real (reduz pé deslizando).
    const float nominal = (useBase || standAim || moveAim) ? 0.0f : nominalSpeed(g_gait, anim);
    float rate = nominal > 0.0f ? s.speed / nominal : 1.0f;
    rate = rate < 0.5f ? 0.5f : rate > 2.0f ? 2.0f : rate;
    if (!g_inAction && std::fabs(rate - g_rate) > 0.05f && now - g_lastRateMs >= 100) {
        g_lastRateMs = now;
        if (puppet::setAnimationRate(rate)) g_rate = rate;
    }

    diagnose(s, !anim.rootBone.empty());
    if (!puppet::move(s.pos, s.yaw, anim.rootBone)) {
        log::warn("remote_player: fantoche sumiu; recriando");
        g_alive = false;
    }
    g_lastSetPos = s.pos;
    g_haveLastSet = true;
    measureRootOffset(nowSec, s.speed);
}

}  // namespace sodcoop::remote_player
