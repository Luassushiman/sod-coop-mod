#include "game/zombies.h"

#include <windows.h>

#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <map>
#include <vector>

#include "core/clock.h"
#include "core/log.h"
#include "game/anim_watch.h"
#include "game/puppet.h"
#include "net/session.h"

namespace sodcoop::zombies {
namespace {

constexpr float kPi = 3.14159265f;
constexpr double kHostInterval = 0.1;       // s entre coletas no host
constexpr double kClientCheckInterval = 10;  // s entre conferências de zumbis locais no cliente
constexpr float kTeleportDistance = 10.0f;

// Erros sobem com error(): o ExecuteBuffer devolve false e o C++ registra no log.
constexpr const char* kLuaHelpers = R"lua(
SodCoop = SodCoop or {}
SodCoop.zp = SodCoop.zp or {}
SodCoop.zBone = SodCoop.zBone or { x = 0, y = 0, z = 0 }
-- Host: nomes dos zumbis do jogo perto de dois pontos, separados por espaço.
function SodCoop.ListZombies(x1, y1, z1, x2, y2, z2, r)
  local seen, out = {}, {}
  for _, c in ipairs({ { x = x1, y = y1, z = z1 }, { x = x2, y = y2, z = z2 } }) do
    for _, e in pairs(System.GetEntitiesInSphere(c, r) or {}) do
      local n = e:GetName()
      if not seen[n] and string.sub(n, 1, 8) == "ULZombie" then
        seen[n] = true
        out[#out + 1] = n
      end
    end
  end
  return table.concat(out, " ")
end
-- Host: dano de um golpe do cliente no zumbi "ULZombie<id>". Mata pelo caminho
-- normal do jogo (Kill: animação de morte e corpo); só SetHealth(0) apagaria o zumbi.
function SodCoop.ZDamage(id, dmg)
  local z = System.GetEntityByName("ULZombie" .. id)
  if not z or not z.actor then return "sem zumbi" end
  if z:IsDead() then return "ja morto" end
  local h = z.actor:GetHealth()
  if h - dmg <= 0 then
    z:Kill(true, g_localActorId, NULL_ENTITY, false, false)
    return string.format("morto (%.0f -> 0)", h)
  end
  z.actor:SetHealth(h - dmg)
  return string.format("vida %.0f -> %.0f", h, h - dmg)
end
function SodCoop.ZAlive(id)
  local e = SodCoop.zp[id]
  return e ~= nil and System.GetEntity(e.id) ~= nil
end
-- Cliente: fantoche de zumbi (AnimObject sem física), como o do jogador.
function SodCoop.ZSpawn(id, model, x, y, z)
  if SodCoop.ZAlive(id) then return end
  if AnimObject == nil then Script.ReloadScript("Scripts/Entities/Physics/AnimObject.lua") end
  local p = new(AnimObject.Properties)
  p.object_Model = model
  p.Physics = p.Physics or {}
  p.Physics.bPhysicalize = 0
  p.Physics.bRigidBody = 0
  p.Physics.bPushableByPlayers = 0
  local e = System.SpawnEntity({ class = "AnimObject", name = "sodcoop_z" .. id,
                                 position = { x = x, y = y, z = z }, properties = p })
  if not e then error("sodcoop: fantoche de zumbi nao criado") end
  SodCoop.zp[id] = e
end
function SodCoop.ZRemove(id)
  if SodCoop.ZAlive(id) then System.RemoveEntity(SodCoop.zp[id].id) end
  SodCoop.zp[id] = nil
end
function SodCoop.ZClear()
  for id, _ in pairs(SodCoop.zp) do SodCoop.ZRemove(id) end
end
function SodCoop.ZAnim(id, name, loop)
  if not SodCoop.ZAlive(id) then return end
  SodCoop.zp[id]:StartAnimation(0, name, 0, 0.2, 1, loop)
end
-- d = { id, x, y, z, yaw, id, x, ... }. Compensa o deslocamento de raiz pelo osso
-- bone (como o fantoche do jogador). Entidades que sumiram são ignoradas.
function SodCoop.ZMoveAll(d, bone)
  for i = 1, #d, 5 do
    local e = SodCoop.zp[d[i]]
    if e and System.GetEntity(e.id) then
      local x, y, z = d[i + 1], d[i + 2], d[i + 3]
      e:SetWorldAngles({ x = 0, y = 0, z = d[i + 4] })
      e:SetWorldPos({ x = x, y = y, z = z })
      if bone ~= "" then
        local b = e:GetBonePos(bone, SodCoop.zBone)
        if b then
          local dx, dy = b.x - x, b.y - y
          if dx * dx + dy * dy < 36 then e:SetWorldPos({ x = x - dx, y = y - dy, z = z }) end
        end
      end
    end
  end
end
)lua";

bool g_helpersReady = false;

bool ensureHelpers() {
    if (g_helpersReady) return true;
    g_helpersReady = puppet::ensureLuaHelpers() && engine::executeLua(kLuaHelpers, "sodcoop_zombies");
    log::info("zombies: helpers Lua %s", g_helpersReady ? "definidos" : "FALHARAM");
    return g_helpersReady;
}

// Modelo do fantoche a partir do modelo base do zumbi no host (um esqueleto sem
// malha; as partes são montadas pelo jogo). Especiais (feral, juggernaut, bloater,
// screamer, armored) têm um .cdf completo por tipo; os comuns usam o modelo
// simplificado (EigenZombie) do gênero.
std::string puppetModel(const char* hostModel, const Config& cfg) {
    std::string m = hostModel;
    for (char& c : m) c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
    // Especiais: "characters/freaks/<tipo>/skeleton/skeleton.chr" -> ".../<tipo>/<tipo>.cdf".
    const std::string freaks = "characters/freaks/";
    if (m.compare(0, freaks.size(), freaks) == 0) {
        const size_t slash = m.find('/', freaks.size());
        if (slash != std::string::npos) {
            const std::string type = m.substr(freaks.size(), slash - freaks.size());
            return freaks + type + "/" + type + ".cdf";
        }
    }
    if (m.empty() || m.find("skeleton") != std::string::npos)
        return m.find("female") != std::string::npos ? "characters/female/zombie/body/tshirt/low/low.cdf"
                                                     : cfg.model;
    for (char c : m)
        if (!isalnum(static_cast<unsigned char>(c)) && !strchr("_/.-", c)) return cfg.model;
    return m;
}

bool isIdentifier(const char* s) {
    if (!*s) return false;
    for (; *s; ++s)
        if (!isalnum(static_cast<unsigned char>(*s)) && *s != '_') return false;
    return true;
}

// Animações em loop no fantoche: blend spaces e ciclos de locomoção/idle.
bool looksLooping(const std::string& name, bool parametric) {
    if (parametric) return true;
    std::string lower = name;
    for (char& c : lower) c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
    for (const char* k : {"idle", "walk", "run", "shamble", "loop", "crawl", "wander", "sprint"})
        if (lower.find(k) != std::string::npos) return true;
    return false;
}

float wrapAngle(float a) {
    while (a > kPi) a -= 2 * kPi;
    while (a < -kPi) a += 2 * kPi;
    return a;
}

// --- Host ------------------------------------------------------------------
size_t g_lastHostCount = static_cast<size_t>(-1);

// --- Cliente ---------------------------------------------------------------
struct Puppet {
    std::string anim;
    engine::Vec3 pos{};  // posição desenhada no último frame
};
std::map<uint16_t, Puppet> g_puppets;
bool g_clientWorld = false;  // já desligou os zumbis locais neste mundo
double g_nextLocalCheck = 0;

}  // namespace

void updateHost(const Config& cfg, bool inWorld, const engine::Vec3& self, bool otherValid,
                const engine::Vec3& other) {
    static double next = 0;
    const double now = clock::seconds();
    if (!cfg.enabled || !inWorld) {
        net::setLocalZombies({});
        return;
    }
    if (!ensureHelpers()) return;
    for (const net::HitEvent& hit : net::takeHits()) {
        char expr[96];
        snprintf(expr, sizeof(expr), "SodCoop.ZDamage(%u, %.1f)", hit.zombieId, hit.damage);
        std::string result;
        puppet::readLuaText(expr, result);
        log::info("zombies: golpe do cliente (%s, %.0f) no zumbi %u: %s", hit.kind ? "tiro" : "corpo a corpo",
                  hit.damage, hit.zombieId, result.c_str());
    }
    if (now < next) return;
    next = now + kHostInterval;

    const engine::Vec3 o = otherValid ? other : self;
    char expr[256];
    snprintf(expr, sizeof(expr), "SodCoop.ListZombies(%.2f, %.2f, %.2f, %.2f, %.2f, %.2f, %.1f)", self.x, self.y,
             self.z, o.x, o.y, o.z, cfg.radius);
    std::string names;
    if (!puppet::readLuaText(expr, names)) return;

    std::vector<net::ZombieState> list;
    size_t start = 0;
    while (start < names.size()) {
        size_t end = names.find(' ', start);
        if (end == std::string::npos) end = names.size();
        const std::string name = names.substr(start, end - start);
        start = end + 1;
        if (name.size() <= 8) continue;
        void* entity = engine::findEntityByName(name.c_str());
        engine::Matrix34 tm;
        engine::Vec3 angles{};
        if (!entity || !engine::getWorldTM(entity, tm) || !engine::getWorldAngles(entity, angles)) continue;
        net::ZombieState z;
        z.id = static_cast<uint16_t>(atoi(name.c_str() + 8));
        z.pos[0] = tm.m[0][3];
        z.pos[1] = tm.m[1][3];
        z.pos[2] = tm.m[2][3];
        z.yaw = angles.z;
        std::string anim;
        bool parametric = false;
        if (anim_watch::newestLayer0(entity, anim, parametric) && anim[0] != '#') {
            snprintf(z.anim, sizeof(z.anim), "%s", anim.c_str());
            z.loop = looksLooping(anim, parametric);
        }
        const std::string model = anim_watch::modelPath(entity);
        snprintf(z.model, sizeof(z.model), "%s", model.c_str());
        static std::vector<std::string> seenModels;
        if (!model.empty() && std::find(seenModels.begin(), seenModels.end(), model) == seenModels.end()) {
            seenModels.push_back(model);
            log::info("zombies: modelo novo no host: %s (%s)", model.c_str(), name.c_str());
        }
        list.push_back(z);
    }
    if (list.size() != g_lastHostCount) {
        g_lastHostCount = list.size();
        log::info("zombies: host enviando %zu zumbi(s)%s%s", list.size(), list.empty() ? "" : ", ex.: ",
                  list.empty() ? "" : list[0].anim);
    }
    net::setLocalZombies(list);
}

void updateClient(const Config& cfg, bool inWorld, const engine::Vec3& self) {
    if (!inWorld) {
        // Nível descarregado: as entidades já não existem.
        g_puppets.clear();
        g_clientWorld = false;
        return;
    }
    if (!cfg.enabled || !ensureHelpers()) return;
    const double now = clock::seconds();

    // Zumbis do próprio cliente: desligados ao entrar no mundo; conferidos de tempos em tempos.
    if (!g_clientWorld) {
        g_clientWorld = true;
        const bool ok = engine::executeLua(
            "System.ExecuteCommand(\"zm_disable 1\"); System.ExecuteCommand(\"zm_killAll\")", "sodcoop_zoff");
        log::info("zombies: zumbis locais desligados (zm_disable 1 + zm_killAll) -> %s", ok ? "OK" : "FALHOU");
        g_nextLocalCheck = now + kClientCheckInterval;
    } else if (now >= g_nextLocalCheck) {
        g_nextLocalCheck = now + kClientCheckInterval;
        char expr[200];
        snprintf(expr, sizeof(expr), "SodCoop.ListZombies(%.2f, %.2f, %.2f, %.2f, %.2f, %.2f, 150)", self.x,
                 self.y, self.z, self.x, self.y, self.z);
        std::string local;
        if (puppet::readLuaText(expr, local) && !local.empty()) {
            log::warn("zombies: zumbis locais apareceram no cliente (%s); zm_killAll", local.c_str());
            engine::executeLua("System.ExecuteCommand(\"zm_killAll\")", "sodcoop_zkill");
        }
    }

    const std::map<uint16_t, net::RemoteZombie> remote = net::getRemoteZombies();

    // Fantoches de zumbis que o host não manda mais.
    for (auto it = g_puppets.begin(); it != g_puppets.end();) {
        if (remote.count(it->first)) {
            ++it;
            continue;
        }
        char code[64];
        snprintf(code, sizeof(code), "SodCoop.ZRemove(%u)", it->first);
        engine::executeLua(code, "sodcoop_zremove");
        it = g_puppets.erase(it);
    }

    std::string moves = "SodCoop.ZMoveAll({";
    char buf[256];
    for (const auto& [id, rz] : remote) {
        // Interpola do estado anterior para o atual ao longo do intervalo entre eles
        // (atraso de ~1 pacote, 0,1 s).
        const net::ZombieState& a = rz.prev;
        const net::ZombieState& b = rz.cur;
        const double span = rz.curAtSec - rz.prevAtSec;
        float k = span > 0.001 ? static_cast<float>((now - rz.curAtSec) / span) : 1.0f;
        k = k < 0 ? 0 : k > 1 ? 1 : k;
        const float dx = b.pos[0] - a.pos[0], dy = b.pos[1] - a.pos[1], dz = b.pos[2] - a.pos[2];
        if (dx * dx + dy * dy + dz * dz > kTeleportDistance * kTeleportDistance) k = 1;
        const float x = a.pos[0] + dx * k, y = a.pos[1] + dy * k, z = a.pos[2] + dz * k;
        const float yaw = wrapAngle(a.yaw + wrapAngle(b.yaw - a.yaw) * k);

        auto it = g_puppets.find(id);
        if (it == g_puppets.end()) {
            const std::string model = puppetModel(b.model, cfg);
            snprintf(buf, sizeof(buf), "SodCoop.ZSpawn(%u, \"%s\", %.2f, %.2f, %.2f)", id, model.c_str(), x, y, z);
            if (!engine::executeLua(buf, "sodcoop_zspawn")) {
                log::warn("zombies: fantoche do zumbi %u falhou", id);
                continue;
            }
            it = g_puppets.emplace(id, Puppet{}).first;
            log::info("zombies: fantoche do zumbi %u criado com %s (host: %s; %zu no total)", id, model.c_str(),
                      b.model, g_puppets.size());
        }
        if (b.anim[0] && it->second.anim != b.anim && isIdentifier(b.anim)) {
            snprintf(buf, sizeof(buf), "SodCoop.ZAnim(%u, \"%s\", %s)", id, b.anim, b.loop ? "true" : "false");
            engine::executeLua(buf, "sodcoop_zanim");
            it->second.anim = b.anim;
        }
        it->second.pos = {x, y, z};
        snprintf(buf, sizeof(buf), "%u,%.3f,%.3f,%.3f,%.4f,", id, x, y, z, yaw);
        moves += buf;
    }
    if (g_puppets.empty()) return;
    moves += "}, \"" + cfg.rootBone + "\")";
    static int moveFailures = 0;
    if (!engine::executeLua(moves.c_str(), "sodcoop_zmove") && ++moveFailures <= 5)
        log::warn("zombies: ZMoveAll falhou");
}

void onLocalAttack(const Config& cfg, AttackKind kind, const engine::Vec3& self, float yaw,
                   const engine::Vec3& aimDir) {
    if (!cfg.enabled || g_puppets.empty()) return;
    constexpr float kMeleeRange = 2.8f;     // m, horizontal
    constexpr float kMeleeCos = 0.34f;      // ~70 graus para cada lado
    constexpr float kShotRange = 60.0f;     // m
    constexpr float kShotRadius = 1.0f;     // m da linha da mira ao centro do zumbi
    constexpr float kChest = 1.4f, kZombieCenter = 1.0f;
    const float fx = -std::sin(yaw), fy = std::cos(yaw);
    float dl = std::sqrt(aimDir.x * aimDir.x + aimDir.y * aimDir.y + aimDir.z * aimDir.z);
    const engine::Vec3 d = dl > 0.01f ? engine::Vec3{aimDir.x / dl, aimDir.y / dl, aimDir.z / dl}
                                      : engine::Vec3{fx, fy, 0};
    uint16_t best = 0;
    float bestScore = 1e9f;
    for (const auto& [id, p] : g_puppets) {
        if (kind == AttackKind::Melee) {
            const float dx = p.pos.x - self.x, dy = p.pos.y - self.y;
            const float dist = std::sqrt(dx * dx + dy * dy);
            if (dist > kMeleeRange || std::fabs(p.pos.z - self.z) > 1.5f) continue;
            if (dist > 0.3f && (dx * fx + dy * fy) / dist < kMeleeCos) continue;
            if (dist < bestScore) bestScore = dist, best = id;
        } else {
            const float ox = p.pos.x - self.x, oy = p.pos.y - self.y, oz = p.pos.z + kZombieCenter - (self.z + kChest);
            const float t = ox * d.x + oy * d.y + oz * d.z;
            if (t <= 0 || t > kShotRange) continue;
            const float px = ox - d.x * t, py = oy - d.y * t, pz = oz - d.z * t;
            if (px * px + py * py + pz * pz > kShotRadius * kShotRadius) continue;
            if (t < bestScore) bestScore = t, best = id;
        }
    }
    if (bestScore >= 1e9f) {
        log::info("zombies: %s do cliente nao acertou nenhum zumbi", kind == AttackKind::Melee ? "golpe" : "tiro");
        return;
    }
    net::HitEvent hit;
    hit.zombieId = best;
    hit.kind = static_cast<uint8_t>(kind);
    hit.damage = kind == AttackKind::Melee ? cfg.meleeDamage : cfg.shotDamage;
    net::sendHit(hit);
    log::info("zombies: %s do cliente acertou o zumbi %u (%.1f m); enviando %.0f de dano",
              kind == AttackKind::Melee ? "golpe" : "tiro", best, bestScore, hit.damage);
}

}  // namespace sodcoop::zombies
