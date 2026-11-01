#include "game/local_player.h"

#include <MinHook.h>
#include <atomic>

#include "core/log.h"

namespace sodcoop::local_player {
namespace {

using SetLocalActor_t = void(__thiscall*)(void* channel, engine::EntityId id);
SetLocalActor_t g_original = nullptr;
std::atomic<engine::EntityId> g_localId{0};

// __fastcall com EDX ignorado reproduz a convenção __thiscall (this em ECX).
void __fastcall setLocalActorDetour(void* channel, void* /*edx*/, engine::EntityId id) {
    engine::EntityId previous = g_localId.exchange(id);
    log::info("local_player: SetLocalActor(channel=%p, id=%u) (anterior=%u)", channel, id, previous);
    g_original(channel, id);
    void* entity = id ? engine::getEntity(id) : nullptr;
    log::info("local_player: GetEntity(%u) -> %p", id, entity);
}

}  // namespace

bool installHook() {
    MH_STATUS s = MH_Initialize();
    if (s != MH_OK && s != MH_ERROR_ALREADY_INITIALIZED) {
        log::error("local_player: MH_Initialize falhou (%s)", MH_StatusToString(s));
        return false;
    }
    auto target = reinterpret_cast<void*>(engine::kSetLocalActor);
    s = MH_CreateHook(target, reinterpret_cast<void*>(&setLocalActorDetour),
                      reinterpret_cast<void**>(&g_original));
    if (s != MH_OK) {
        log::error("local_player: MH_CreateHook(%p) falhou (%s)", target, MH_StatusToString(s));
        return false;
    }
    s = MH_EnableHook(target);
    if (s != MH_OK) {
        log::error("local_player: MH_EnableHook falhou (%s)", MH_StatusToString(s));
        return false;
    }
    log::info("local_player: hook instalado em %p", target);
    return true;
}

engine::EntityId currentId() { return g_localId.load(); }

bool sample(Snapshot& out) {
    out.id = currentId();
    if (!out.id) return false;
    void* entity = engine::getEntity(out.id);
    if (!entity) return false;
    engine::Matrix34 tm;
    if (!engine::getWorldTM(entity, tm)) return false;
    out.pos = {tm.m[0][3], tm.m[1][3], tm.m[2][3]};
    return engine::getWorldAngles(entity, out.angles);
}

}  // namespace sodcoop::local_player
