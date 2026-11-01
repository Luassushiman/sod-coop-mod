#include "game/puppet.h"

#include <cstdio>

#include "core/log.h"

namespace sodcoop::puppet {
namespace {

bool g_helpersReady = false;
constexpr const char* kPuppetName = "sodcoop_puppet";  // igual ao name do SpawnEntity no Lua

// Erros sobem com error(): o ExecuteBuffer devolve false e o C++ registra no log.
constexpr const char* kLuaHelpers = R"lua(
SodCoop = SodCoop or {}
function SodCoop.PuppetAlive()
  local e = SodCoop.puppet
  return e ~= nil and System.GetEntity(e.id) ~= nil
end
-- SodCoop.stage indica até onde o spawn chegou (o C++ consulta quando falha).
function SodCoop.SpawnPuppet(model, x, y, z)
  if SodCoop.PuppetAlive() then return end
  SodCoop.stage = 1
  if AnimObject == nil then Script.ReloadScript("Scripts/Entities/Physics/AnimObject.lua") end
  SodCoop.stage = 2
  -- AnimObject (deriva de BasicEntity): modelo em object_Model, sem física para não colidir.
  local p = new(AnimObject.Properties)
  p.object_Model = model
  p.Physics = p.Physics or {}
  p.Physics.bPhysicalize = 0
  p.Physics.bRigidBody = 0
  p.Physics.bPushableByPlayers = 0
  SodCoop.stage = 3
  local e = System.SpawnEntity({ class = "AnimObject", name = "sodcoop_puppet",
                                 position = { x = x, y = y, z = z }, properties = p })
  if not e then error("sodcoop: System.SpawnEntity devolveu nil") end
  SodCoop.stage = 4
  SodCoop.puppet = e
  SodCoop.stage = 5
end
function SodCoop.RemovePuppet()
  if SodCoop.PuppetAlive() then System.RemoveEntity(SodCoop.puppet.id) end
  SodCoop.puppet = nil
end
function SodCoop.PlayPuppetAnim(name, speed)
  if not SodCoop.PuppetAlive() then error("sodcoop: fantoche inexistente") end
  local e = SodCoop.puppet
  e:StartAnimation(0, name, 0, 0.25, speed, true)
  e:ForceCharacterUpdate(0, true)
end
function SodCoop.SetPuppetAnimSpeed(rate)
  if not SodCoop.PuppetAlive() then error("sodcoop: fantoche inexistente") end
  SodCoop.puppet:SetAnimationSpeed(0, 0, rate)
end
-- Os clipes de locomoção carregam deslocamento de raiz que um AnimObject não
-- consome: o corpo avança dentro do ciclo e "volta" ao reiniciar o loop.
-- Compensação: depois de posicionar a entidade, mede onde o osso raiz ficou
-- (horizontal) e desloca a entidade pela diferença, para o corpo ficar no alvo.
SodCoop.tmpBone = SodCoop.tmpBone or { x = 0, y = 0, z = 0 }
-- Canal Lua -> DLL: o ExecuteBuffer não devolve valores, então o Lua grava
-- dados como posição de uma entidade invisível ("sonda") que a DLL lê nativamente.
function SodCoop.Report(a, b, c)
  local p = SodCoop.probe
  if p == nil or System.GetEntity(p.id) == nil then
    if BasicEntity == nil then Script.ReloadScript("Scripts/Entities/Physics/BasicEntity.lua") end
    -- Sem modelo: BasicEntity:SetFromProperties retorna cedo (nada carregado, sem física).
    local props = new(BasicEntity.Properties)
    props.object_Model = ""
    p = System.SpawnEntity({ class = "BasicEntity", name = "sodcoop_probe",
                             position = { x = a, y = b, z = c }, properties = props })
    SodCoop.probe = p
  end
  if p then p:SetWorldPos({ x = a, y = b, z = c }) end
end
function SodCoop.MovePuppet(x, y, z, yaw, bone)
  if not SodCoop.PuppetAlive() then error("sodcoop: fantoche inexistente") end
  local e = SodCoop.puppet
  e:SetWorldAngles({ x = 0, y = 0, z = yaw })
  e:SetWorldPos({ x = x, y = y, z = z })
  local dx, dy, got = 0, 0, 0
  if bone ~= nil and bone ~= "" then
    local b = e:GetBonePos(bone, SodCoop.tmpBone)
    if b then
      dx, dy, got = b.x - x, b.y - y, 1
      if dx * dx + dy * dy < 36 then  -- ignora leituras absurdas (> 6 m); o ciclo de corrida chega a ~3,1 m
        e:SetWorldPos({ x = x - dx, y = y - dy, z = z })
      end
    end
  end
  SodCoop.Report(dx, dy, 5000 + got)  -- z alto: longe do mundo jogável
end
)lua";

}  // namespace

bool ensureLuaHelpers() {
    if (g_helpersReady) return true;
    g_helpersReady = engine::executeLua(kLuaHelpers, "sodcoop_helpers");
    log::info("puppet: helpers Lua %s", g_helpersReady ? "definidos" : "FALHARAM");
    return g_helpersReady;
}

bool spawn(const std::string& model, const engine::Vec3& pos, int drivenMotion) {
    if (!ensureLuaHelpers()) return false;
    char code[512];
    snprintf(code, sizeof(code), "SodCoop.SpawnPuppet(\"%s\", %.3f, %.3f, %.3f)", model.c_str(), pos.x,
             pos.y, pos.z);
    bool ok = engine::executeLua(code, "sodcoop_spawn");
    log::info("puppet: spawn em (%.2f, %.2f, %.2f) modelo '%s' -> %s", pos.x, pos.y, pos.z,
              model.c_str(), ok ? "OK" : "FALHOU");
    if (ok && drivenMotion >= 0) {
        // Os clipes de locomoção têm deslocamento de raiz; o modo certo deve
        // impedir o corpo de avançar dentro do ciclo e "voltar" ao reiniciar o loop.
        void* entity = engine::findEntityByName(kPuppetName);
        bool set = entity && engine::setAnimationDrivenMotion(entity, drivenMotion != 0);
        log::info("puppet: entidade '%s' = %p; driven_motion=%d -> %s", kPuppetName, entity, drivenMotion,
                  set ? "OK" : "FALHOU");
    }
    if (!ok) {
        // Sem acesso à mensagem de erro do Lua: descobre o estágio por eliminação.
        int stage = -1;
        for (int i = 0; i <= 5 && stage < 0; ++i) {
            snprintf(code, sizeof(code), "assert(SodCoop.stage ~= %d)", i);
            if (!engine::executeLua(code, "sodcoop_probe")) stage = i;
        }
        log::warn("puppet: spawn parou no estagio %d (1=script AnimObject, 2=propriedades, "
                  "3=SpawnEntity, 4=registro, -1=desconhecido)", stage);
    }
    return ok;
}

bool move(const engine::Vec3& pos, float yaw, const std::string& rootBone) {
    char code[256];
    snprintf(code, sizeof(code), "SodCoop.MovePuppet(%.3f, %.3f, %.3f, %.4f, \"%s\")", pos.x, pos.y, pos.z, yaw,
             rootBone.c_str());
    return engine::executeLua(code, "sodcoop_move");
}

bool playAnimation(const std::string& name, float speed) {
    char code[256];
    snprintf(code, sizeof(code), "SodCoop.PlayPuppetAnim(\"%s\", %.3f)", name.c_str(), speed);
    bool ok = engine::executeLua(code, "sodcoop_anim");
    if (!ok) log::warn("puppet: animacao '%s' falhou", name.c_str());
    return ok;
}

bool currentPosition(engine::Vec3& out) {
    void* entity = engine::findEntityByName(kPuppetName);
    engine::Matrix34 tm;
    if (!entity || !engine::getWorldTM(entity, tm)) return false;
    out = {tm.m[0][3], tm.m[1][3], tm.m[2][3]};
    return true;
}

bool readProbe(engine::Vec3& out) {
    void* entity = engine::findEntityByName("sodcoop_probe");
    engine::Matrix34 tm;
    if (!entity || !engine::getWorldTM(entity, tm)) return false;
    out = {tm.m[0][3], tm.m[1][3], tm.m[2][3]};
    return true;
}

bool setAnimationRate(float rate) {
    char code[96];
    snprintf(code, sizeof(code), "SodCoop.SetPuppetAnimSpeed(%.3f)", rate);
    return engine::executeLua(code, "sodcoop_animspeed");
}

void remove() {
    if (!g_helpersReady) return;
    bool ok = engine::executeLua("SodCoop.RemovePuppet()", "sodcoop_remove");
    log::info("puppet: removido -> %s", ok ? "OK" : "FALHOU");
}

}  // namespace sodcoop::puppet
