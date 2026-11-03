#include "game/vehicles.h"

#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

#include "core/clock.h"
#include "core/log.h"
#include "game/engine.h"
#include "game/puppet.h"
#include "game/remote_player.h"

namespace sodcoop::vehicles {
namespace {

constexpr float kPi = 3.14159265f;
constexpr double kRenderDelay = 0.1;  // s atrás do estado mais novo (interpola entre dois)

// Erros sobem com error(): o ExecuteBuffer devolve false e o C++ registra no log.
constexpr const char* kLuaHelpers = R"lua(
SodCoop = SodCoop or {}
-- Carro em que o jogador local está: "classe x y z ax ay az idHex" ou "".
function SodCoop.LocalVehicle()
  local a = g_localActor
  if not a or not a.actor then return "" end
  local ok, vid = pcall(function() return a.actor:GetLinkedVehicleId() end)
  if not ok or not vid then return "" end
  local v = System.GetEntity(vid)
  if not v or not v.vehicle then return "" end
  local p, g = v:GetWorldPos(), v:GetWorldAngles()
  local id = string.match(tostring(v.id), "(%x+)$") or "0"
  return string.format("%s %.3f %.3f %.3f %.4f %.4f %.4f %s", v.class, p.x, p.y, p.z, g.x, g.y, g.z, id)
end
-- Carros do outro jogador, um por carro dele (chave = id do carro lá). Sem física
-- enquanto ele dirige (movido a cada frame); com física quando estacionado.
SodCoop.rvs = SodCoop.rvs or {}
function SodCoop.RemoteVehicle(key, class, x, y, z, ax, ay, az)
  local e = SodCoop.rvs[key]
  if e and (not System.GetEntity(e.id) or e.class ~= class) then
    if System.GetEntity(e.id) then System.RemoveEntity(e.id) end
    e = nil
  end
  if not e then
    e = System.SpawnEntity({ class = class, name = "sodcoop_vehicle_" .. key, position = { x = x, y = y, z = z } })
    if not e then error("sodcoop: carro nao criado: " .. class) end
    SodCoop.rvs[key] = e
    e.sodcoopParked = true
  end
  if e.sodcoopParked then
    e:EnablePhysics(false)
    e.sodcoopParked = false
  end
  e:SetWorldPos({ x = x, y = y, z = z })
  e:SetWorldAngles({ x = ax, y = ay, z = az })
end
-- O outro jogador saiu: o carro fica onde está, com física (colide e fica no chão).
function SodCoop.ParkRemoteVehicle(key)
  local e = SodCoop.rvs[key]
  if not e or not System.GetEntity(e.id) then SodCoop.rvs[key] = nil; return end
  e:EnablePhysics(true)
  e.sodcoopParked = true
end
function SodCoop.RemoveRemoteVehicles()
  for k, e in pairs(SodCoop.rvs) do
    if System.GetEntity(e.id) then System.RemoveEntity(e.id) end
  end
  SodCoop.rvs = {}
end
)lua";

bool g_helpersReady = false;

bool ensureHelpers() {
    if (g_helpersReady) return true;
    g_helpersReady = puppet::ensureLuaHelpers() && engine::executeLua(kLuaHelpers, "sodcoop_vehicles");
    log::info("vehicles: helpers Lua %s", g_helpersReady ? "definidos" : "FALHARAM");
    return g_helpersReady;
}

bool isClassName(const char* s) {
    if (!*s || strlen(s) > 30) return false;
    for (; *s; ++s)
        if (!isalnum(static_cast<unsigned char>(*s)) && *s != '_') return false;
    return true;
}

float wrapAngle(float a) {
    while (a > kPi) a -= 2 * kPi;
    while (a < -kPi) a += 2 * kPi;
    return a;
}

struct Sample {
    std::string cls;
    float pos[3] = {}, ang[3] = {};
    double at = 0;
};
Sample g_prev, g_cur;
float g_seat[3] = {};         // posição do jogador relativa ao carro (eixos do carro, só yaw)
unsigned g_posedSpawn = ~0u;  // fantoche (remote_player::spawnCount) que já está na pose de dirigir
constexpr const char* kDrivePose = "Vehicle_truck_steer";  // animação do jogador dirigindo (t 0,5 = volante reto)
uint32_t g_lastSeq = 0;
uint32_t g_driving = 0;        // id (no outro lado) do carro que ele está dirigindo; 0 = a pé
uint32_t g_lastKnownCar = 0;   // último carro em que ele esteve (estacionado ao sair)
bool g_anyCar = false;         // há carros-fantoche criados (para remover ao desconectar)
double g_outSince = 0;         // fora de carro desde (para não piscar ao trocar de carro)

}  // namespace

void fillLocal(bool settled, net::PlayerState& s) {
    s.vehicle[0] = 0;
    if (!settled || !ensureHelpers()) return;
    std::string text;
    if (!puppet::readLuaText("SodCoop.LocalVehicle()", text) || text.empty()) return;
    char cls[32] = {};
    float p[3], g[3];
    unsigned id = 0;
    if (sscanf(text.c_str(), "%31s %f %f %f %f %f %f %x", cls, &p[0], &p[1], &p[2], &g[0], &g[1], &g[2], &id) != 8 ||
        !isClassName(cls))
        return;
    s.vehicleId = id;
    snprintf(s.vehicle, sizeof(s.vehicle), "%s", cls);
    memcpy(s.vehiclePos, p, sizeof(p));
    memcpy(s.vehicleAngles, g, sizeof(g));
    static std::string last;
    if (last != cls) {
        last = cls;
        log::info("vehicles: jogador local entrou num %s", cls);
    }
}

bool updateRemote(const net::RemoteState& remote, bool settled) {
    const double now = clock::seconds();
    const bool inVehicle = remote.connected && remote.state.hasPlayer && isClassName(remote.state.vehicle);
    if (!settled) {
        g_driving = g_lastKnownCar = 0;  // nível descarregado: as entidades já não existem
        g_anyCar = false;
        return inVehicle;
    }
    if (!remote.connected && g_anyCar) {
        engine::executeLua("SodCoop.RemoveRemoteVehicles()", "sodcoop_vehicle");
        log::info("vehicles: carros do outro jogador removidos (desconectado)");
        g_anyCar = false;
        g_driving = g_lastKnownCar = 0;
    }
    if (!inVehicle) {
        if (g_driving) {
            if (g_outSince == 0) g_outSince = now;
            if (now - g_outSince > 0.5) {
                char code[80];
                snprintf(code, sizeof(code), "SodCoop.ParkRemoteVehicle(%u)", g_driving);
                engine::executeLua(code, "sodcoop_vehicle");
                log::info("vehicles: carro %u do outro jogador estacionado (saiu do carro)", g_driving);
                g_driving = 0;
            }
        }
        g_cur.cls.clear();
        return false;
    }
    g_outSince = 0;
    if (!ensureHelpers()) return true;

    // Amostras novas do estado (20 Hz): guarda a anterior e a atual.
    if (remote.sequence != g_lastSeq) {
        g_lastSeq = remote.sequence;
        g_prev = g_cur;
        g_cur.cls = remote.state.vehicle;
        memcpy(g_cur.pos, remote.state.vehiclePos, sizeof(g_cur.pos));
        memcpy(g_cur.ang, remote.state.vehicleAngles, sizeof(g_cur.ang));
        g_cur.at = now;
        if (g_prev.cls != g_cur.cls) g_prev = g_cur;
        // Banco: a posição do jogador (que o jogo põe no assento) nos eixos do carro.
        const float dx = remote.state.pos[0] - g_cur.pos[0], dy = remote.state.pos[1] - g_cur.pos[1];
        const float c = std::cos(-g_cur.ang[2]), s = std::sin(-g_cur.ang[2]);
        g_seat[0] = dx * c - dy * s;
        g_seat[1] = dx * s + dy * c;
        g_seat[2] = remote.state.pos[2] - g_cur.pos[2];
    }
    if (g_cur.cls.empty()) return true;

    // Interpola da anterior para a atual, kRenderDelay atrás.
    const double span = g_cur.at - g_prev.at;
    float k = span > 0.001 ? static_cast<float>((now - kRenderDelay - g_prev.at) / span) : 1.0f;
    k = k < 0 ? 0 : k > 1 ? 1 : k;
    float p[3], g[3];
    for (int i = 0; i < 3; ++i) {
        p[i] = g_prev.pos[i] + (g_cur.pos[i] - g_prev.pos[i]) * k;
        g[i] = wrapAngle(g_prev.ang[i] + wrapAngle(g_cur.ang[i] - g_prev.ang[i]) * k);
    }
    const uint32_t car = remote.state.vehicleId ? remote.state.vehicleId : 1;
    if (g_driving && g_driving != car) {  // trocou de carro sem passar 0,5 s a pé
        char park[80];
        snprintf(park, sizeof(park), "SodCoop.ParkRemoteVehicle(%u)", g_driving);
        engine::executeLua(park, "sodcoop_vehicle");
    }
    char code[256];
    snprintf(code, sizeof(code), "SodCoop.RemoteVehicle(%u, \"%s\", %.3f, %.3f, %.3f, %.4f, %.4f, %.4f)", car,
             g_cur.cls.c_str(), p[0], p[1], p[2], g[0], g[1], g[2]);
    const bool ok = engine::executeLua(code, "sodcoop_vehicle");
    if (ok && g_driving != car) {
        log::info("vehicles: outro jogador dirigindo o carro %u (%s)%s", car, g_cur.cls.c_str(),
                  car == g_lastKnownCar ? ", o mesmo de antes" : "");
        g_driving = g_lastKnownCar = car;
        g_anyCar = true;
    }
    static int failures = 0;
    if (!ok && ++failures <= 5) log::warn("vehicles: RemoteVehicle falhou (%s)", g_cur.cls.c_str());

    // Motorista: o fantoche no banco, com a pose de dirigir, girando com o carro.
    if (puppet::entity()) {
        const float c = std::cos(g[2]), s = std::sin(g[2]);
        const engine::Vec3 seat{p[0] + g_seat[0] * c - g_seat[1] * s, p[1] + g_seat[0] * s + g_seat[1] * c,
                                p[2] + g_seat[2]};
        puppet::move(seat, g[2], "");
        const unsigned spawn = remote_player::spawnCount();
        if (spawn != g_posedSpawn && puppet::holdPose(kDrivePose, 0.5f, 0)) g_posedSpawn = spawn;
    }
    return true;
}

bool needsPuppetRespawn() {
    // Depois de dirigir, o fantoche está na pose de volante: recria em pé ao sair.
    if (g_posedSpawn == ~0u) return false;
    g_posedSpawn = ~0u;
    return true;
}

}  // namespace sodcoop::vehicles
