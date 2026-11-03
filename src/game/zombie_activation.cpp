#include "game/zombie_activation.h"

#include <MinHook.h>

#include <cstdint>

#include "core/log.h"

namespace sodcoop::zombie_activation {
namespace {

// FUN_0140DED0: cdecl () -> lista de carros (mov eax,[0x01E56AD4]; ret). O ZombieManager a
// chama logo depois de pôr os aliados na lista de pontos; o retorno nesse ponto é 0x01340711.
constexpr uintptr_t kGetVehicleList = 0x0140DED0;
constexpr uintptr_t kAfterAlliesReturn = 0x01340711;
// Lista de pontos de interesse do ZombieManager (até 20).
constexpr uintptr_t kPointCount = 0x01DEE754;  // u32
constexpr uintptr_t kPointPos = 0x01E1CAA0;    // float[3] por ponto
constexpr uintptr_t kPointDir = 0x01E1CB90;    // float[3] por ponto (eixo do corpo)
constexpr uintptr_t kPointType = 0x01DF0958;   // u8: 0 jogador, 1 aliado, 2 carro...
constexpr uintptr_t kPointIndex = 0x01DF096C;  // u8: índice na lista de origem (aliados: 0..99)
constexpr uintptr_t kPointFlags = 0x01DF0980;  // u16
constexpr uint32_t kMaxPoints = 20;
// Tipo e índice do ponto ([zombies] client_point_type / client_point_index; em teste).
uint8_t g_type = 1;
uint8_t g_index = 99;

using GetList_t = void*(__cdecl*)();
GetList_t g_original = nullptr;

volatile bool g_valid = false;
engine::Vec3 g_pos{};
uint32_t g_added = 0;

void* __cdecl getListDetour() {
    if (g_valid && reinterpret_cast<uintptr_t>(__builtin_return_address(0)) == kAfterAlliesReturn) {
        auto& count = *reinterpret_cast<uint32_t*>(kPointCount);
        if (count < kMaxPoints) {
            const uint32_t i = count;
            float* pos = reinterpret_cast<float*>(kPointPos + i * 12);
            float* dir = reinterpret_cast<float*>(kPointDir + i * 12);
            pos[0] = g_pos.x;
            pos[1] = g_pos.y;
            pos[2] = g_pos.z;
            dir[0] = 0;
            dir[1] = 1;
            dir[2] = 0;
            reinterpret_cast<uint8_t*>(kPointType)[i] = g_type;
            reinterpret_cast<uint8_t*>(kPointIndex)[i] = g_index;
            reinterpret_cast<uint16_t*>(kPointFlags)[i] = 0;
            count = i + 1;
            if (++g_added == 1) log::info("zombie_activation: cliente na lista de pontos (ponto %u)", i);
        }
    }
    return g_original();
}

}  // namespace

bool installHook(int type, int index) {
    g_type = static_cast<uint8_t>(type);
    g_index = static_cast<uint8_t>(index);
    log::info("zombie_activation: ponto do cliente com tipo %d, indice %d", type, index);
    void* target = reinterpret_cast<void*>(kGetVehicleList);
    MH_STATUS s = MH_CreateHook(target, reinterpret_cast<void*>(&getListDetour), reinterpret_cast<void**>(&g_original));
    if (s == MH_OK) s = MH_EnableHook(target);
    log::info("zombie_activation: hook em %p -> %s", target, MH_StatusToString(s));
    return s == MH_OK;
}

void setClient(bool valid, const engine::Vec3& pos) {
    g_pos = pos;
    g_valid = valid;
}

}  // namespace sodcoop::zombie_activation
