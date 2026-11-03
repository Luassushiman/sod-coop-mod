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
-- Carro em que o jogador local está: "classe x y z ax ay az idHex nome ox oy oz" ou ""
-- (o = posição do jogador nos eixos do carro, só yaw: no volante, o lugar do motorista).
-- No carro do outro jogador: "P chave assento" (passageiro); se o jogo o pôs no volante
-- (ao entrar, ou o "usar" que passa o passageiro para o volante), volta para o banco do
-- passageiro. A posição do passageiro não serve: o jogo a deixa no ponto do motorista.
function SodCoop.LocalVehicle()
  local a = g_localActor
  if not a or not a.actor then return "" end
  local ok, vid = pcall(function() return a.actor:GetLinkedVehicleId() end)
  if not ok or not vid then return "" end
  local v = System.GetEntity(vid)
  if not v or not v.vehicle then return "" end
  local p, g = v:GetWorldPos(), v:GetWorldAngles()
  for key, e in pairs(SodCoop.rvs or {}) do
    if tostring(e.id) == tostring(v.id) then
      local seat = v.vehicle:GetSeatForPassenger(a.id) or 0
      -- Entrando pela porta do passageiro, o jogo desliza para o volante vazio. ChangeSeat
      -- recebe uma escolha, não o assento: 2 leva do volante (1) para o lado dele (3).
      if seat == 1 then v.vehicle:ChangeSeat(a.id, 2, false) end
      return string.format("P %s %d", tostring(key), seat)
    end
  end
  local q = a:GetWorldPos()
  local dx, dy = q.x - p.x, q.y - p.y
  local c, s = math.cos(-g.z), math.sin(-g.z)
  local id = string.match(tostring(v.id), "(%x+)$") or "0"
  local name = string.gsub(v:GetName() or "", "[^%w_]", "")
  if name == "" then name = "-" end
  return string.format("%s %.3f %.3f %.3f %.4f %.4f %.4f %s %s %.3f %.3f %.3f", v.class, p.x, p.y, p.z, g.x, g.y,
    g.z, id, name, dx * c - dy * s, dx * s + dy * c, q.z - p.z)
end
-- Carros do outro jogador, um por carro dele (chave = id do carro lá). Sem física
-- enquanto ele dirige (movido a cada frame); com física quando estacionado. Com o save
-- copiado, o carro dele costuma existir aqui também (mesmo nome e classe, outro id):
-- esse é usado ("emprestado"); senão, um carro é criado.
SodCoop.rvs = SodCoop.rvs or {}
-- O mesmo carro no mundo daqui: mesma classe e nome, até 30 m, o mais perto, que não
-- seja o meu nem já usado por outra chave.
function SodCoop.FindSameCar(class, name, x, y, z)
  if name == "-" then return nil end
  local mine = g_localActor and g_localActor.actor and g_localActor.actor:GetLinkedVehicleId()
  local best, bestD
  for _, w in pairs(System.GetEntitiesInSphere({ x = x, y = y, z = z }, 30) or {}) do
    if w.vehicle and w.class == class and w:GetName() == name and tostring(w.id) ~= tostring(mine) then
      local used = false
      for _, e in pairs(SodCoop.rvs) do if tostring(e.id) == tostring(w.id) then used = true end end
      local p = w:GetWorldPos()
      local d = (p.x - x) ^ 2 + (p.y - y) ^ 2 + (p.z - z) ^ 2
      if not used and (not bestD or d < bestD) then best, bestD = w, d end
    end
  end
  return best
end
function SodCoop.RemoteVehicle(key, class, name, x, y, z, ax, ay, az)
  local e = SodCoop.rvs[key]
  if e and (not System.GetEntity(e.id) or e.class ~= class) then
    if System.GetEntity(e.id) and not e.sodcoopBorrowed then System.RemoveEntity(e.id) end
    SodCoop.rvs[key] = nil
    e = nil
  end
  if not e then
    e = SodCoop.FindSameCar(class, name, x, y, z)
    if e then
      e.sodcoopBorrowed = true
    else
      e = System.SpawnEntity({ class = class, name = "sodcoop_vehicle_" .. key, position = { x = x, y = y, z = z } })
      if not e then error("sodcoop: carro nao criado: " .. class) end
    end
    SodCoop.rvs[key] = e
    e.sodcoopParked = true
    e.sodcoopTries = 0
  end
  -- Criado logo ao entrar no mundo: o carro igual daqui pode ainda não aparecer na busca.
  -- Procura de novo a cada ~60 chamadas (~1 s) e, achando, troca o criado por ele.
  if not e.sodcoopBorrowed and e.sodcoopTries < 30 then
    e.sodcoopTick = (e.sodcoopTick or 0) + 1
    if e.sodcoopTick % 60 == 0 then
      e.sodcoopTries = e.sodcoopTries + 1
      local w = SodCoop.FindSameCar(class, name, x, y, z)
      if w then
        System.RemoveEntity(e.id)
        e = w
        e.sodcoopBorrowed = true
        e.sodcoopParked = true
        SodCoop.rvs[key] = e
        System.Log("sodcoop: carro criado trocado pelo mesmo carro daqui " .. tostring(e.id))
      end
    end
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
    if System.GetEntity(e.id) then
      if e.sodcoopBorrowed then e:EnablePhysics(true) else System.RemoveEntity(e.id) end
    end
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
unsigned g_posedSpawn = ~0u;  // fantoche (remote_player::spawnCount) que já está numa pose de carro
bool g_posedSitting = false;   // essa pose é a de passageiro (senão, a de dirigir)
constexpr const char* kDrivePose = "Vehicle_truck_steer";  // animação do jogador dirigindo (t 0,5 = volante reto)
// Passageiro sentado: nomes = "Vehicle_" + arquivo em animations/characters/hero_male/vehicle/.
constexpr const char* kSitPoses[] = {"Vehicle_pickupold_passenger_idle", "Vehicle_car_passenger_idle_generic"};
uint32_t g_myCar = 0;          // carro em que o jogador local está ou esteve por último (id aqui)
float g_myCarPos[3] = {}, g_myCarAng[3] = {};
float g_mySeat[3] = {-0.42f, 0.48f, 1.0f};  // meu lugar no volante, nos eixos do carro (Suv_New medido)
uint32_t g_lastSeq = 0;
uint32_t g_driving = 0;        // id (no outro lado) do carro que ele está dirigindo; 0 = a pé
uint32_t g_lastKnownCar = 0;   // último carro em que ele esteve (estacionado ao sair)
bool g_anyCar = false;         // há carros-fantoche criados (para remover ao desconectar)
double g_outSince = 0;         // fora de carro desde (para não piscar ao trocar de carro)

}  // namespace

void fillLocal(bool settled, net::PlayerState& s) {
    s.vehicle[0] = 0;
    s.passengerOf = 0;
    if (!settled || !ensureHelpers()) return;
    std::string text;
    if (!puppet::readLuaText("SodCoop.LocalVehicle()", text) || text.empty()) return;
    if (text[0] == 'P') {
        unsigned key = 0;
        int seat = 0;
        if (sscanf(text.c_str(), "P %u %d", &key, &seat) != 2) return;
        s.passengerOf = key;
        s.passengerSeat = static_cast<uint8_t>(seat);
        static unsigned lastKey = 0;
        if (lastKey != key) {
            lastKey = key;
            log::info("vehicles: jogador local de passageiro no carro %u do outro jogador (assento %d)", key, seat);
        }
        return;
    }
    char cls[32] = {};
    float p[3], g[3];
    unsigned id = 0;
    char name[32] = {};
    float o[3];
    if (sscanf(text.c_str(), "%31s %f %f %f %f %f %f %x %31s %f %f %f", cls, &p[0], &p[1], &p[2], &g[0], &g[1], &g[2],
               &id, name, &o[0], &o[1], &o[2]) != 12 ||
        !isClassName(cls))
        return;
    if (std::fabs(o[0]) < 3 && std::fabs(o[1]) < 3 && std::fabs(o[2]) < 3) memcpy(g_mySeat, o, sizeof(o));
    snprintf(s.vehicleName, sizeof(s.vehicleName), "%s", isClassName(name) ? name : "-");
    s.vehicleId = id;
    g_myCar = id;
    memcpy(g_myCarPos, p, sizeof(p));
    memcpy(g_myCarAng, g, sizeof(g));
    snprintf(s.vehicle, sizeof(s.vehicle), "%s", cls);
    memcpy(s.vehiclePos, p, sizeof(p));
    memcpy(s.vehicleAngles, g, sizeof(g));
    static std::string last;
    if (last != cls) {
        last = cls;
        log::info("vehicles: jogador local entrou num %s", cls);
    }
}

bool remoteSeated(const net::RemoteState& remote) {
    if (!remote.connected || !remote.state.hasPlayer) return false;
    return isClassName(remote.state.vehicle) || (remote.state.passengerOf && remote.state.passengerOf == g_myCar);
}

namespace {
// Outro jogador de passageiro no meu carro: o fantoche no banco dele, sentado. A posição
// do carro é a última lida em fillLocal (parado, se eu já saí dele). O banco do
// passageiro é o do motorista espelhado (lado direito). Bancos de trás: não medidos.
void updatePassenger(const net::RemoteState& remote) {
    if (!puppet::entity()) return;
    const float* p = g_myCarPos;
    const float o[3] = {-g_mySeat[0], g_mySeat[1], g_mySeat[2]};
    const float yaw = g_myCarAng[2];
    const float c = std::cos(yaw), s = std::sin(yaw);
    const engine::Vec3 seat{p[0] + o[0] * c - o[1] * s, p[1] + o[0] * s + o[1] * c, p[2] + o[2]};
    puppet::move(seat, yaw, "");
    const unsigned spawn = remote_player::spawnCount();
    if (spawn == g_posedSpawn && g_posedSitting) return;
    for (const char* pose : kSitPoses) {
        if (puppet::holdPose(pose, 0.5f, 0)) {
            g_posedSpawn = spawn;
            g_posedSitting = true;
            log::info("vehicles: outro jogador de passageiro no meu carro %u (pose %s)", g_myCar, pose);
            return;
        }
    }
    static int failures = 0;
    if (++failures <= 3) log::warn("vehicles: nenhuma pose de passageiro funcionou");
}
}  // namespace

bool updateRemote(const net::RemoteState& remote, bool settled) {
    const double now = clock::seconds();
    const bool inVehicle = remote.connected && remote.state.hasPlayer && isClassName(remote.state.vehicle);
    if (!settled) {
        g_myCar = 0;
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
        if (remoteSeated(remote)) updatePassenger(remote);
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
    const char* name = isClassName(remote.state.vehicleName) ? remote.state.vehicleName : "-";
    snprintf(code, sizeof(code), "SodCoop.RemoteVehicle(%u, \"%s\", \"%s\", %.3f, %.3f, %.3f, %.4f, %.4f, %.4f)",
             car, g_cur.cls.c_str(), name, p[0], p[1], p[2], g[0], g[1], g[2]);
    const bool ok = engine::executeLua(code, "sodcoop_vehicle");
    if (ok && g_driving != car) {
        char expr[96];
        snprintf(expr, sizeof(expr), "SodCoop.rvs[%u].sodcoopBorrowed and 'o mesmo carro daqui' or 'carro criado'",
                 car);
        std::string how;
        puppet::readLuaText(expr, how);
        log::info("vehicles: outro jogador dirigindo o carro %u (%s %s, %s)%s", car, g_cur.cls.c_str(), name,
                  how.c_str(), car == g_lastKnownCar ? ", o mesmo de antes" : "");
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
        if ((spawn != g_posedSpawn || g_posedSitting) && puppet::holdPose(kDrivePose, 0.5f, 0)) {
            g_posedSpawn = spawn;
            g_posedSitting = false;
        }
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
