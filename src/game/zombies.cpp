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
#include "game/appearance.h"
#include "game/puppet.h"
#include "net/session.h"

namespace sodcoop::zombies {
namespace {

constexpr float kPi = 3.14159265f;
constexpr double kHostInterval = 0.1;       // s entre coletas no host
constexpr double kClientCheckInterval = 10;  // s entre conferências de zumbis locais no cliente
constexpr float kTeleportDistance = 10.0f;
// Aparência (partes de pele) de cada zumbi: o host lê ao ver o zumbi e de tempos em tempos.
constexpr double kLookReadInterval = 3.0;    // s entre releituras
constexpr double kLookRetryInterval = 0.3;   // s, enquanto o jogo ainda não prendeu as partes
constexpr double kLookResendInterval = 3.0;  // s entre reenvios ao cliente
constexpr size_t kLookSendsPerTick = 4;
// Cliente: espera a aparência antes de criar o fantoche (senão usa o modelo simplificado).
constexpr double kLookWaitUnknown = 1.0;  // s, host ainda sem partes (lookHash 0)
constexpr double kLookWait = 3.5;         // s, hash conhecido mas aparência ainda não chegou
constexpr int kSpawnsPerFrame = 4;

// Erros sobem com error(): o ExecuteBuffer devolve false e o C++ registra no log.
constexpr const char* kLuaHelpers = R"lua(
SodCoop = SodCoop or {}
SodCoop.zp = SodCoop.zp or {}
SodCoop.zBone = SodCoop.zBone or { x = 0, y = 0, z = 0 }
SodCoop.zc = SodCoop.zc or {}  -- host: zumbis conduzidos pelo mod (EntityId -> estado)
SodCoop.zById = SodCoop.zById or {}  -- host: EntityId -> entidade de zumbi vista
SodCoop.zStrikes = { "ZombieMelee_strike_left_fast", "ZombieMelee_strike_right_fast", "ZombieMelee_attack_close" }
SodCoop.zSpeed = { Feral = 6.5, Juggernaut = 2.8, Bloater = 2.2 }  -- m/s; os outros 3,5
SodCoop.zReact = "ZombieReact_mid_front_heavy"  -- reação a golpe (vista num zumbi do host)
SodCoop.zNow = SodCoop.zNow or 0
-- EntityId numérico: o Lua só tem o handle, que o tostring mostra em hexadecimal
-- ("userdata: 0002FCFA"). O nome "ULZombie<n>" não serve de id: o jogo repete.
function SodCoop.EId(e)
  return tonumber(string.match(tostring(e.id), "(%x+)$"), 16)
end
-- Host: zumbis do jogo perto de dois pontos ("EntityId nome", separados por espaço).
function SodCoop.ListZombies(x1, y1, z1, x2, y2, z2, r)
  local seen, out = {}, {}
  for _, c in ipairs({ { x = x1, y = y1, z = z1 }, { x = x2, y = y2, z = z2 } }) do
    for _, e in pairs(System.GetEntitiesInSphere(c, r) or {}) do
      local n = e:GetName()
      local id = SodCoop.EId(e)
      if id and not seen[id] and string.sub(n, 1, 8) == "ULZombie" then
        seen[id] = true
        SodCoop.zById[id] = e
        -- "id=animação" quando o mod conduz o zumbi (ZChase): o cliente mostra essa.
        -- "id~": zumbi morto (o cliente não mira nele).
        local z = SodCoop.zc[id]
        if e:IsDead() then out[#out + 1] = id .. "~"
        else out[#out + 1] = (z and z.anim) and (id .. "=" .. z.anim) or tostring(id) end
      end
    end
  end
  return table.concat(out, " ")
end
-- Entidade de zumbi pelo EntityId (nil se já não existe).
function SodCoop.ZEntity(id)
  local z = SodCoop.zById[id]
  if z and System.GetEntity(z.id) then return z end
  SodCoop.zById[id] = nil
  return nil
end
-- Host: dano de um golpe do cliente no zumbi id. Mata pelo caminho normal do jogo
-- (Kill: animação de morte e corpo); só SetHealth(0) apagaria o zumbi.
function SodCoop.ZDamage(id, dmg)
  local z = SodCoop.ZEntity(id)
  if not z or not z.actor then return "sem zumbi" end
  if z:IsDead() then return "ja morto" end
  local h = z.actor:GetHealth()
  if h - dmg <= 0 then
    z:Kill(true, g_localActorId, NULL_ENTITY, false, false)
    return string.format("morto (%.0f -> 0)", h)
  end
  z.actor:SetHealth(h - dmg)
  -- Zumbi conduzido: reage ao golpe (para, cancela o golpe dele e toca a reação).
  local c = SodCoop.zc[id]
  if c then
    c.anim = SodCoop.zReact
    c.hpAnim = nil
    c.hitAt = nil
    c.busyUntil = SodCoop.zNow + 0.45
    c.strikeAt = math.max(c.strikeAt, SodCoop.zNow - 1.0)
  end
  return string.format("vida %.0f -> %.0f", h, h - dmg)
end
-- Host: a IA do jogo só persegue o jogador local, então o mod conduz os zumbis
-- vivos que estão a menos de aggro m do jogador do cliente (t) e mais perto dele
-- do que do host (h): vira para ele, corre (SetWorldPos) e golpeia a cada 1,8 s.
-- Solta o zumbi quando o cliente se afasta ou o host fica mais perto. Devolve os
-- ids dos zumbis cujo golpe acertou neste frame (0,4 s depois do início do golpe,
-- se o cliente ainda estiver a até 2,2 m), separados por espaço. Vários zumbis
-- ficam a pelo menos 0,9 m um do outro, em volta do cliente.
function SodCoop.ZChase(tx, ty, tz, hx, hy, hz, dt, now, aggro, bone)
  SodCoop.zNow = now
  local t = SodCoop.zTarget or {}
  t.x, t.y, t.z = tx, ty, tz
  SodCoop.zTarget = t
  local hits, seen, placed = {}, {}, {}
  for _, e in pairs(System.GetEntitiesInSphere(t, aggro + 8) or {}) do
    local n = e:GetName()
    local id = SodCoop.EId(e)
    if id and string.sub(n, 1, 8) == "ULZombie" and e.actor and not e:IsDead() and e.actor:GetHealth() > 0 then
      SodCoop.zById[id] = e
      local p = e:GetWorldPos()
      local dx, dy = tx - p.x, ty - p.y
      local dc = math.sqrt(dx * dx + dy * dy)
      local dh = math.sqrt((hx - p.x) ^ 2 + (hy - p.y) ^ 2)
      local c = SodCoop.zc[id]
      if not c and dc < aggro and dc < dh then
        c = { strikeAt = -100, n = 0, busyUntil = 0 }
        SodCoop.zc[id] = c
      elseif c and (dc > aggro + 6 or dh < dc - 3) then
        SodCoop.zc[id] = nil
        c = nil
      end
      if c then
        seen[id] = true
        if dc > 0.01 then
          c.yaw = math.atan2(-dx, dy)
          e:SetWorldAngles({ x = 0, y = 0, z = c.yaw })
        end
        if c.hitAt and now >= c.hitAt then
          c.hitAt = nil
          if dc <= 2.2 then hits[#hits + 1] = tostring(id) end
        end
        local np = { x = p.x, y = p.y, z = p.z }
        if now < c.busyUntil then
          -- golpe em andamento: fica parado
        elseif dc > 1.6 then  -- para a 1,3 m; golpeia a até 1,6 m
          local s = math.min(dc - 1.3, (SodCoop.zSpeed[e.class] or 3.5) * dt)
          np.x, np.y = p.x + dx / dc * s, p.y + dy / dc * s
          np.z = p.z + (tz - p.z) * math.min(1, dt * 4)
          c.anim = "ZombieMelee_run_forward_loop"
        elseif now - c.strikeAt >= 1.8 then
          c.strikeAt = now
          c.n = c.n + 1
          c.anim = SodCoop.zStrikes[(c.n - 1) % #SodCoop.zStrikes + 1]
          c.busyUntil = now + 0.9
          c.hitAt = now + 0.4
        else
          c.anim = "Test_wander_idle"
        end
        -- Afasta dos zumbis já posicionados neste frame e não deixa entrar no cliente.
        for _, q in ipairs(placed) do
          local ox, oy = np.x - q.x, np.y - q.y
          local od = math.sqrt(ox * ox + oy * oy)
          if od < 0.9 then
            if od < 0.01 then ox, oy, od = -dy, dx, math.max(dc, 0.01) end
            np.x, np.y = np.x + ox / od * (0.9 - od), np.y + oy / od * (0.9 - od)
          end
        end
        local rx, ry = np.x - tx, np.y - ty
        local rd = math.sqrt(rx * rx + ry * ry)
        if rd < 1.0 and rd > 0.01 then np.x, np.y = tx + rx / rd * 1.0, ty + ry / rd * 1.0 end
        if np.x ~= p.x or np.y ~= p.y or np.z ~= p.z then e:SetWorldPos(np) end
        placed[#placed + 1] = np
        -- Fantoche do host (HAdopt): segue o zumbi e toca a animação escolhida aqui.
        local hp = SodCoop.hA[id] and SodCoop.zp[id]
        if hp and System.GetEntity(hp.id) then
          SodCoop.ZPlace(hp, np.x, np.y, np.z, c.yaw or 0, bone)
          if c.anim and c.hpAnim ~= c.anim then
            hp:StartAnimation(0, c.anim, 0, 0.2, 1, c.anim ~= SodCoop.zStrikes[1] and c.anim ~= SodCoop.zStrikes[2]
                              and c.anim ~= SodCoop.zStrikes[3] and c.anim ~= SodCoop.zReact)
            c.hpAnim = c.anim
          end
        end
      end
    end
  end
  for id, _ in pairs(SodCoop.zc) do
    if not seen[id] then SodCoop.zc[id] = nil end
  end
  return table.concat(hits, " ")
end
-- Cliente: golpe de zumbi do host no jogador local. Se zerar a vida, mata pelo
-- caminho normal do jogo (Kill: queda e tela de morte, como na morte comum).
function SodCoop.PlayerHurt(dmg)
  local a = g_localActor
  if not a or not a.actor then return "sem jogador" end
  if a:IsDead() then return "ja morto" end
  local h = a.actor:GetHealth()
  if h - dmg <= 0 then
    a:Kill(true, NULL_ENTITY, NULL_ENTITY, false, false)
    return string.format("morto (%.0f -> 0)", h)
  end
  a.actor:SetHealth(h - dmg)
  if a.actor.CameraShake then pcall(a.actor.CameraShake, a.actor, 0.5, 0.25, 0.1, a:GetWorldPos()) end
  return string.format("vida %.0f -> %.0f", h, h - dmg)
end
-- Cliente: impacto do golpe do jogador local no fantoche id, sem esperar o host: o fantoche
-- reage na hora e a câmera treme de leve.
function SodCoop.ZHitFeedback(id, melee)
  if SodCoop.ZAlive(id) then SodCoop.zp[id]:StartAnimation(0, SodCoop.zReact, 0, 0.1, 1, false) end
  local a = g_localActor
  if melee and a and a.actor and a.actor.CameraShake then
    pcall(a.actor.CameraShake, a.actor, 0.7, 0.2, 0.1, a:GetWorldPos())
  end
end
function SodCoop.ZAlive(id)
  local e = SodCoop.zp[id]
  return e ~= nil and System.GetEntity(e.id) ~= nil
end
-- Cliente: fantoche de zumbi (AnimObject sem física), como o do jogador. Devolve o
-- EntityId do fantoche (as partes são presas pelo C++, pela entidade).
function SodCoop.ZSpawn(id, model, x, y, z)
  if SodCoop.ZAlive(id) then return SodCoop.EId(SodCoop.zp[id]) end
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
  return SodCoop.EId(e)
end
function SodCoop.ZRemove(id)
  if SodCoop.ZAlive(id) then System.RemoveEntity(SodCoop.zp[id].id) end
  SodCoop.zp[id] = nil
end
function SodCoop.ZClear()
  for id, _ in pairs(SodCoop.zp) do SodCoop.ZRemove(id) end
end
-- Host: o fantoche zp[id] passa a mostrar o zumbi conduzido id; o zumbi fica sem desenho.
SodCoop.hA = SodCoop.hA or {}
function SodCoop.HAdopt(id)
  local z = SodCoop.ZEntity(id)
  if not z or not SodCoop.ZAlive(id) then return "sem zumbi" end
  z:DrawSlot(0, 0)
  SodCoop.hA[id] = true
  return "ok"
end
-- Host: tira o fantoche e devolve o desenho ao zumbi (vivo ou morto).
function SodCoop.HRelease(id)
  SodCoop.ZRemove(id)
  SodCoop.hA[id] = nil
  local z = SodCoop.ZEntity(id)
  if z then z:DrawSlot(0, 1) end
end
-- Cliente: zumbi morto no host (lá ele cai por física, sem animação de morte). Como o
-- fantoche do jogador (SodCoop.PuppetRagdoll): step 0 liga a física; step 1 solta o corpo.
function SodCoop.ZRagdoll(id, step)
  if not SodCoop.ZAlive(id) then return end
  local e = SodCoop.zp[id]
  if step == 0 then
    e.Properties.Physics.bPhysicalize = 1
    e.Properties.Physics.bRigidBody = 1
    e.Properties.Physics.Mass = 80  -- sem massa, o ragdoll ficou pendurado em pé (2026-11-03)
    e.Properties.Physics.Density = -1
    e:PhysicalizeThis()
    return
  end
  e:Event_RagDollize()
  e:AwakePhysics(1)
end
function SodCoop.ZAnim(id, name, loop)
  if not SodCoop.ZAlive(id) then return end
  SodCoop.zp[id]:StartAnimation(0, name, 0, 0.2, 1, loop)
end
-- d = { id, x, y, z, yaw, id, x, ... }. Compensa o deslocamento de raiz pelo osso
-- bone (como o fantoche do jogador). Entidades que sumiram são ignoradas.
function SodCoop.ZPlace(e, x, y, z, yaw, bone)
  e:SetWorldAngles({ x = 0, y = 0, z = yaw })
  e:SetWorldPos({ x = x, y = y, z = z })
  if bone ~= "" then
    local b = e:GetBonePos(bone, SodCoop.zBone)
    if b then
      local dx, dy = b.x - x, b.y - y
      if dx * dx + dy * dy < 36 then e:SetWorldPos({ x = x - dx, y = y - dy, z = z }) end
    end
  end
end
function SodCoop.ZMoveAll(d, bone)
  for i = 1, #d, 5 do
    local e = SodCoop.zp[d[i]]
    if e and System.GetEntity(e.id) then SodCoop.ZPlace(e, d[i + 1], d[i + 2], d[i + 3], d[i + 4], bone) end
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

// FNV-1a; nunca 0 (0 = sem aparência).
uint32_t hashText(const std::string& s) {
    uint32_t h = 2166136261u;
    for (unsigned char c : s) h = (h ^ c) * 16777619u;
    return h ? h : 1;
}

// --- Host ------------------------------------------------------------------
size_t g_lastHostCount = static_cast<size_t>(-1);
bool g_chasing = false;  // SodCoop.zc pode ter zumbis conduzidos

struct HostLook {
    std::string text;  // "" = o jogo ainda não prendeu as partes
    uint32_t hash = 0;
    double nextRead = 0, nextSend = 0;
    bool seen = false;  // na lista desta rodada
};
std::map<uint32_t, HostLook> g_hostLooks;  // EntityId do zumbi -> aparência
std::map<uint32_t, bool> g_hostPuppets;    // zumbi conduzido com fantoche no host -> visto nesta rodada

void releaseHostPuppet(uint32_t id) {
    char code[64];
    snprintf(code, sizeof(code), "SodCoop.HRelease(%u)", id);
    engine::executeLua(code, "sodcoop_hrelease");
}

void releaseAllHostPuppets() {
    for (const auto& [id, seen] : g_hostPuppets) releaseHostPuppet(id);
    if (!g_hostPuppets.empty()) log::info("zombies: %zu fantoche(s) do host removidos", g_hostPuppets.size());
    g_hostPuppets.clear();
}

// Zumbi conduzido pelo mod: fantoche com a aparência dele no lugar (o zumbi fica sem desenho).
void adoptHostPuppet(const Config& cfg, uint32_t id, const net::ZombieState& z, const HostLook& look) {
    const std::string base = appearance::baseModel(look.text);
    const std::string model = base.empty() ? puppetModel(z.model, cfg) : base;
    char code[256];
    snprintf(code, sizeof(code), "SodCoop.ZSpawn(%u, \"%s\", %.2f, %.2f, %.2f)", id, model.c_str(), z.pos[0],
             z.pos[1], z.pos[2]);
    std::string eid, result;
    if (!puppet::readLuaText(code, eid)) {
        log::warn("zombies: fantoche do host para o zumbi %u falhou", id);
        return;
    }
    int added = 0, failed = 0;
    if (!base.empty()) {
        void* entity = engine::getEntity(static_cast<engine::EntityId>(strtoul(eid.c_str(), nullptr, 10)));
        if (entity) added = appearance::attachParts(entity, look.text, failed);
    }
    snprintf(code, sizeof(code), "SodCoop.HAdopt(%u)", id);
    puppet::readLuaText(code, result);
    g_hostPuppets[id] = true;
    log::info("zombies: fantoche do host no zumbi conduzido %u (%s, %d parte(s), %d falha(s)): %s", id, model.c_str(),
              added, failed, result.c_str());
}

// A cada frame: o mod conduz os zumbis perto do jogador do cliente (SodCoop.ZChase)
// e manda ao cliente os golpes que acertaram.
void updateChase(const Config& cfg, double now, const engine::Vec3& self, bool otherValid,
                 const engine::Vec3& other) {
    static double last = 0;
    const double dt = last > 0 ? std::min(now - last, 0.1) : 0.0;
    last = now;
    if (!cfg.chase || !otherValid) {
        releaseAllHostPuppets();
        if (g_chasing) engine::executeLua("SodCoop.zc = {}", "sodcoop_zchase");
        g_chasing = false;
        return;
    }
    g_chasing = true;
    char expr[320];
    snprintf(expr, sizeof(expr), "SodCoop.ZChase(%.2f, %.2f, %.2f, %.2f, %.2f, %.2f, %.4f, %.3f, %.1f, \"%s\")",
             other.x, other.y, other.z, self.x, self.y, self.z, dt, now, cfg.aggroRadius,
             cfg.hostPuppets ? cfg.rootBone.c_str() : "");
    std::string hits;
    if (!puppet::readLuaText(expr, hits)) {
        static int failures = 0;
        if (++failures <= 5) log::warn("zombies: ZChase falhou");
        return;
    }
    for (size_t start = 0; start < hits.size();) {
        size_t end = hits.find(' ', start);
        if (end == std::string::npos) end = hits.size();
        net::HitEvent hit;
        hit.zombieId = static_cast<uint32_t>(strtoul(hits.c_str() + start, nullptr, 10));
        hit.kind = 2;
        hit.damage = cfg.attackDamage;
        net::sendHit(hit);
        log::info("zombies: zumbi %u golpeou o jogador do cliente (%.0f de dano)", hit.zombieId, hit.damage);
        start = end + 1;
    }
}

// --- Cliente ---------------------------------------------------------------
struct Puppet {
    std::string anim;
    engine::Vec3 pos{};     // posição desenhada no último frame
    bool dead = false;      // morto no host: golpes e tiros não miram nele
    int ragdollStep = 0;    // corpo mole depois da morte (SodCoop.ZRagdoll); 3 = feito
    double ragdollAt = 0;
    uint32_t lookHash = 0;  // aparência montada (0 = modelo simplificado, sem a do host)
};
std::map<uint32_t, Puppet> g_puppets;  // EntityId do zumbi no host -> fantoche
std::map<uint32_t, double> g_waitingLook;
// Golpe corpo a corpo detectado no início do movimento: dano e impacto saem no contato.
constexpr double kMeleeContactDelay = 0.3;  // s
struct PendingHit {
    net::HitEvent hit;
    double at = 0;
};
std::vector<PendingHit> g_pendingHits;

void deliverHit(const net::HitEvent& hit) {
    char code[64];
    snprintf(code, sizeof(code), "SodCoop.ZHitFeedback(%u, %s)", hit.zombieId, hit.kind == 0 ? "true" : "false");
    engine::executeLua(code, "sodcoop_zhitfx");
    // A reação já tocou aqui: quando o host mandar a mesma animação, não recomeça.
    auto it = g_puppets.find(hit.zombieId);
    if (it != g_puppets.end()) it->second.anim = "ZombieReact_mid_front_heavy";
    net::sendHit(hit);
}  // zumbi sem fantoche ainda -> desde quando espera a aparência
bool g_clientWorld = false;  // já desligou os zumbis locais neste mundo
double g_nextLocalCheck = 0;

}  // namespace

void updateHost(const Config& cfg, bool inWorld, const engine::Vec3& self, bool otherValid,
                const engine::Vec3& other) {
    static double next = 0;
    const double now = clock::seconds();
    if (!cfg.enabled || !inWorld) {
        if (!inWorld) g_hostPuppets.clear();  // nível descarregado: as entidades já não existem
        net::setLocalZombies({});
        return;
    }
    if (!ensureHelpers()) return;
    for (const net::HitEvent& hit : net::takeHits()) {
        if (hit.kind > 1) continue;
        char expr[96];
        snprintf(expr, sizeof(expr), "SodCoop.ZDamage(%u, %.1f)", hit.zombieId, hit.damage);
        std::string result;
        puppet::readLuaText(expr, result);
        log::info("zombies: golpe do cliente (%s, %.0f) no zumbi %u: %s", hit.kind ? "tiro" : "corpo a corpo",
                  hit.damage, hit.zombieId, result.c_str());
    }
    updateChase(cfg, now, self, otherValid, other);
    if (now < next) return;
    next = now + kHostInterval;

    const engine::Vec3 o = otherValid ? other : self;
    char expr[256];
    snprintf(expr, sizeof(expr), "SodCoop.ListZombies(%.2f, %.2f, %.2f, %.2f, %.2f, %.2f, %.1f)", self.x, self.y,
             self.z, o.x, o.y, o.z, cfg.radius);
    std::string names;
    if (!puppet::readLuaText(expr, names)) return;

    std::vector<net::ZombieState> list;
    std::vector<net::ZombieLook> looks;
    int adopted = 0;
    size_t start = 0;
    while (start < names.size()) {
        size_t end = names.find(' ', start);
        if (end == std::string::npos) end = names.size();
        const std::string item = names.substr(start, end - start);
        start = end + 1;
        // "EntityId" ou "EntityId=animação" (animação escolhida pelo ZChase, se o mod conduz o zumbi).
        std::string chaseAnim;
        const size_t eq = item.find('=');
        if (eq != std::string::npos) chaseAnim = item.substr(eq + 1);
        const bool dead = item.find('~') != std::string::npos;
        const engine::EntityId id = static_cast<engine::EntityId>(strtoul(item.c_str(), nullptr, 10));
        if (id == 0) continue;
        void* entity = engine::getEntity(id);
        engine::Matrix34 tm;
        engine::Vec3 angles{};
        if (!entity || !engine::getWorldTM(entity, tm) || !engine::getWorldAngles(entity, angles)) continue;
        net::ZombieState z;
        z.id = id;
        z.dead = dead;
        z.pos[0] = tm.m[0][3];
        z.pos[1] = tm.m[1][3];
        z.pos[2] = tm.m[2][3];
        z.yaw = angles.z;
        std::string anim;
        bool parametric = false;
        if (!chaseAnim.empty()) {
            snprintf(z.anim, sizeof(z.anim), "%s", chaseAnim.c_str());
            z.loop = looksLooping(chaseAnim, false);
        } else if (anim_watch::newestLayer0(entity, anim, parametric) && anim[0] != '#') {
            snprintf(z.anim, sizeof(z.anim), "%s", anim.c_str());
            z.loop = looksLooping(anim, parametric);
        }
        const std::string model = anim_watch::modelPath(entity);
        snprintf(z.model, sizeof(z.model), "%s", model.c_str());
        static std::vector<std::string> seenModels;
        if (!model.empty() && std::find(seenModels.begin(), seenModels.end(), model) == seenModels.end()) {
            seenModels.push_back(model);
            log::info("zombies: modelo novo no host: %s (entidade %u)", model.c_str(), id);
        }
        HostLook& look = g_hostLooks[id];
        look.seen = true;
        if (now >= look.nextRead) {
            int parts = 0;
            std::string text = appearance::describe(entity, &parts);
            if (parts == 0) text.clear();
            if (text != look.text) {
                look.text = text;
                look.hash = text.empty() ? 0 : hashText(text);
                look.nextSend = 0;
                static int logged = 0;
                if (!text.empty() && ++logged <= 12)
                    log::info("zombies: aparencia do zumbi %u (%d parte(s)): %s", id, parts, text.c_str());
            }
            look.nextRead = now + (text.empty() ? kLookRetryInterval : kLookReadInterval);
        }
        z.lookHash = look.hash;
        if (otherValid && look.hash && now >= look.nextSend && looks.size() < kLookSendsPerTick) {
            looks.push_back({id, look.hash, look.text});
            look.nextSend = now + kLookResendInterval;
        }
        if (cfg.hostPuppets && !chaseAnim.empty()) {
            auto hp = g_hostPuppets.find(id);
            if (hp != g_hostPuppets.end()) hp->second = true;
            else if (adopted < 4) {
                ++adopted;
                adoptHostPuppet(cfg, id, z, look);
            }
        }
        list.push_back(z);
    }
    // Fantoches do host de zumbis que o mod já não conduz (soltos ou mortos).
    for (auto it = g_hostPuppets.begin(); it != g_hostPuppets.end();) {
        if (it->second) {
            it->second = false;
            ++it;
            continue;
        }
        releaseHostPuppet(it->first);
        log::info("zombies: fantoche do host do zumbi %u removido", it->first);
        it = g_hostPuppets.erase(it);
    }
    for (auto it = g_hostLooks.begin(); it != g_hostLooks.end();) {
        if (!it->second.seen) {
            it = g_hostLooks.erase(it);
            continue;
        }
        it->second.seen = false;
        ++it;
    }
    if (!looks.empty()) net::sendZombieLooks(looks);
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
        g_waitingLook.clear();
        g_pendingHits.clear();
        g_clientWorld = false;
        return;
    }
    if (!cfg.enabled || !ensureHelpers()) return;
    const double now = clock::seconds();

    for (const net::HitEvent& hit : net::takeHits()) {
        if (hit.kind != 2) continue;
        char expr[64];
        snprintf(expr, sizeof(expr), "SodCoop.PlayerHurt(%.1f)", hit.damage);
        std::string result;
        puppet::readLuaText(expr, result);
        log::info("zombies: zumbi %u do host golpeou o jogador (%.0f): %s", hit.zombieId, hit.damage, result.c_str());
    }

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

    for (auto it = g_pendingHits.begin(); it != g_pendingHits.end();) {
        if (now < it->at) {
            ++it;
            continue;
        }
        deliverHit(it->hit);
        it = g_pendingHits.erase(it);
    }

    const std::map<uint32_t, net::RemoteZombie> remote = net::getRemoteZombies();

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
    for (auto it = g_waitingLook.begin(); it != g_waitingLook.end();)
        it = remote.count(it->first) ? std::next(it) : g_waitingLook.erase(it);
    int spawns = 0;

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

        std::string look;
        const bool haveLook = b.lookHash && net::getZombieLook(id, b.lookHash, look);
        auto it = g_puppets.find(id);
        if (it != g_puppets.end() && haveLook && it->second.lookHash != b.lookHash && spawns < kSpawnsPerFrame) {
            // A aparência chegou depois (fantoche com o modelo simplificado) ou mudou: tira o
            // fantoche e cria outro no próximo frame (nunca troca partes de um personagem em uso).
            ++spawns;
            snprintf(buf, sizeof(buf), "SodCoop.ZRemove(%u)", id);
            engine::executeLua(buf, "sodcoop_zremove");
            log::info("zombies: aparencia nova do zumbi %u; recriando o fantoche", id);
            g_puppets.erase(it);
            continue;
        }
        if (it == g_puppets.end()) {
            if (!haveLook) {
                const double since = g_waitingLook.emplace(id, now).first->second;
                if (now - since < (b.lookHash ? kLookWait : kLookWaitUnknown)) continue;
            }
            if (spawns >= kSpawnsPerFrame) continue;
            ++spawns;
            const std::string base = haveLook ? appearance::baseModel(look) : std::string();
            const std::string model = base.empty() ? puppetModel(b.model, cfg) : base;
            snprintf(buf, sizeof(buf), "SodCoop.ZSpawn(%u, \"%s\", %.2f, %.2f, %.2f)", id, model.c_str(), x, y, z);
            std::string eid;
            if (!puppet::readLuaText(buf, eid)) {
                log::warn("zombies: fantoche do zumbi %u falhou", id);
                continue;
            }
            g_waitingLook.erase(id);
            int added = 0, failed = 0;
            if (!base.empty()) {
                void* entity = engine::getEntity(static_cast<engine::EntityId>(strtoul(eid.c_str(), nullptr, 10)));
                if (entity) added = appearance::attachParts(entity, look, failed);
                else failed = -1;
            }
            it = g_puppets.emplace(id, Puppet{}).first;
            // Com a aparência, não tenta de novo mesmo se alguma parte falhar (evita recriar em laço).
            it->second.lookHash = haveLook ? b.lookHash : 0;
            if (base.empty())
                log::info("zombies: fantoche do zumbi %u criado com %s, sem a aparencia (host: %s; %zu no total)", id,
                          model.c_str(), b.model, g_puppets.size());
            else
                log::info("zombies: fantoche do zumbi %u criado com a aparencia do host (%s, %d parte(s), %d falha(s); "
                          "%zu no total)", id, model.c_str(), added, failed, g_puppets.size());
        }
        if (b.dead) {
            // Morto: vira corpo mole e deixa de ser movido/animado (0 física; 0,3 s e 1 s: soltar).
            Puppet& p = it->second;
            if (!p.dead) {
                p.dead = true;
                p.ragdollStep = 0;
                p.ragdollAt = now;
            }
            if (p.ragdollStep <= 2 && now >= p.ragdollAt) {
                snprintf(buf, sizeof(buf), "SodCoop.ZRagdoll(%u, %d)", id, p.ragdollStep == 0 ? 0 : 1);
                if (!engine::executeLua(buf, "sodcoop_zragdoll") || p.ragdollStep == 0)
                    log::info("zombies: zumbi %u morto no host; ragdoll do fantoche passo %d", id, p.ragdollStep);
                p.ragdollAt = now + (p.ragdollStep == 0 ? 0.3 : 0.7);
                ++p.ragdollStep;
            }
            continue;
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
    uint32_t best = 0;
    float bestScore = 1e9f;
    for (const auto& [id, p] : g_puppets) {
        if (p.dead) continue;
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
    if (kind == AttackKind::Melee) g_pendingHits.push_back({hit, clock::seconds() + kMeleeContactDelay});
    else deliverHit(hit);
    log::info("zombies: %s do cliente acertou o zumbi %u (%.1f m); enviando %.0f de dano",
              kind == AttackKind::Melee ? "golpe" : "tiro", best, bestScore, hit.damage);
}

}  // namespace sodcoop::zombies
