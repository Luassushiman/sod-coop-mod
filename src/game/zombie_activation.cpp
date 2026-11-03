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
// Tipo negativo = não põe ponto, só pede a ativação (abaixo).
int g_type = -1;
uint8_t g_index = 99;
// Pedido de ativação: cdecl (u16 zumbi, float prioridade, u8 flags) -> fila de 24 pedidos
// ordenada pela prioridade (0x01DF2D90 zumbi, 0x01DF2D30 prioridade), que o ZombieManager
// atende criando a entidade. DeterminePerception a chama para os zumbis perto do jogador.
constexpr uintptr_t kRequestActivation = 0x0133CF70;
using RequestActivation_t = uint32_t(__cdecl*)(uint32_t, float, uint32_t);
// Zumbis do ZombieManager (simulação "lab"): posição float[3] e flags (bit 31 = vaga em uso).
constexpr uintptr_t kZombiePos = 0x01DF2F80;
constexpr uintptr_t kZombieFlags = 0x01DF4B80;
constexpr int kZombieSlots = 384;  // até 0x01DF5180, o array seguinte
// Estado de percepção por zumbi, que DeterminePerception grava antes de pedir a ativação:
// u16 com 0x10 = "ativar"; u8 com o ponto de interesse mais perto (0 = jogador local).
constexpr uintptr_t kZombiePerception = 0x01DEFD58;
constexpr uintptr_t kZombieNearestPoint = 0x01DF0358;
float g_radius = 15.0f;
float g_priority = 1.0f;
uint32_t g_requests = 0;

using GetList_t = void*(__cdecl*)();
GetList_t g_original = nullptr;

volatile bool g_valid = false;
engine::Vec3 g_pos{};
uint32_t g_added = 0;

void* __cdecl getListDetour() {
    if (g_valid && reinterpret_cast<uintptr_t>(__builtin_return_address(0)) == kAfterAlliesReturn) {
        // Pede a ativação dos zumbis perto do cliente.
        const auto request = reinterpret_cast<RequestActivation_t>(kRequestActivation);
        const auto* zpos = reinterpret_cast<const float*>(kZombiePos);
        const auto* zflags = reinterpret_cast<const int32_t*>(kZombieFlags);
        int asked = 0;
        for (int z = 0; z < kZombieSlots; ++z) {
            if (zflags[z] >= 0) continue;
            const float dx = zpos[z * 3] - g_pos.x, dy = zpos[z * 3 + 1] - g_pos.y;
            if (dx * dx + dy * dy > g_radius * g_radius) continue;
            reinterpret_cast<uint16_t*>(kZombiePerception)[z] |= 0x10;
            reinterpret_cast<uint8_t*>(kZombieNearestPoint)[z] = 0;
            request(static_cast<uint32_t>(z), g_priority, 0);
            ++asked;
        }
        if (asked && (++g_requests == 1 || g_requests % 600 == 0))
            log::info("zombie_activation: %d zumbi(s) a ate %.0f m do cliente pedidos para ativar", asked, g_radius);
        auto& count = *reinterpret_cast<uint32_t*>(kPointCount);
        if (g_type >= 0 && count < kMaxPoints) {
            const uint32_t i = count;
            float* pos = reinterpret_cast<float*>(kPointPos + i * 12);
            float* dir = reinterpret_cast<float*>(kPointDir + i * 12);
            pos[0] = g_pos.x;
            pos[1] = g_pos.y;
            pos[2] = g_pos.z;
            dir[0] = 0;
            dir[1] = 1;
            dir[2] = 0;
            reinterpret_cast<uint8_t*>(kPointType)[i] = static_cast<uint8_t>(g_type);
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
    g_type = type;
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
