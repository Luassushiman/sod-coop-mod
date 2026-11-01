#include "game/frame.h"

#include <MinHook.h>

#include "core/log.h"
#include "game/engine.h"

namespace sodcoop::frame {
namespace {

using PreUpdate_t = bool(__thiscall*)(void* self, bool haveFocus, unsigned int flags);
using PostUpdate_t = void(__thiscall*)(void* self, bool haveFocus, unsigned int flags);
PreUpdate_t g_preOriginal = nullptr;
PostUpdate_t g_postOriginal = nullptr;
Callback g_callbacks[8] = {};
int g_count = 0;
bool g_loggedFirstFrame = false;
bool g_forceFocus = false;
bool g_lastFocus = true;

bool __fastcall preUpdateDetour(void* self, void* /*edx*/, bool haveFocus, unsigned int flags) {
    if (haveFocus != g_lastFocus) {
        g_lastFocus = haveFocus;
        log::info("frame: janela %s o foco%s", haveFocus ? "ganhou" : "perdeu",
                  g_forceFocus ? " (ignorado: keep_running_unfocused=1)" : "");
    }
    return g_preOriginal(self, g_forceFocus ? true : haveFocus, flags);
}

void __fastcall postUpdateDetour(void* self, void* /*edx*/, bool haveFocus, unsigned int flags) {
    if (!g_loggedFirstFrame) {
        g_loggedFirstFrame = true;
        log::info("frame: primeiro PostUpdate (this=%p)", self);
    }
    for (int i = 0; i < g_count; ++i) g_callbacks[i]();
    g_postOriginal(self, g_forceFocus ? true : haveFocus, flags);
}

bool hook(uintptr_t address, void* detour, void** original, const char* name) {
    auto target = reinterpret_cast<void*>(address);
    MH_STATUS s = MH_CreateHook(target, detour, original);
    if (s == MH_OK) s = MH_EnableHook(target);
    if (s != MH_OK) {
        log::error("frame: hook %s em %p falhou (%s)", name, target, MH_StatusToString(s));
        return false;
    }
    log::info("frame: hook instalado em %p (%s)", target, name);
    return true;
}

}  // namespace

void setForceFocus(bool force) { g_forceFocus = force; }

bool addCallback(Callback cb) {
    if (g_count >= 8) return false;
    g_callbacks[g_count++] = cb;
    return true;
}

bool installHook() {
    MH_STATUS s = MH_Initialize();
    if (s != MH_OK && s != MH_ERROR_ALREADY_INITIALIZED) {
        log::error("frame: MH_Initialize falhou (%s)", MH_StatusToString(s));
        return false;
    }
    return hook(engine::kActionPreUpdate, reinterpret_cast<void*>(&preUpdateDetour),
                reinterpret_cast<void**>(&g_preOriginal), "CCryAction::PreUpdate") &&
           hook(engine::kActionPostUpdate, reinterpret_cast<void*>(&postUpdateDetour),
                reinterpret_cast<void**>(&g_postOriginal), "CCryAction::PostUpdate");
}

}  // namespace sodcoop::frame
