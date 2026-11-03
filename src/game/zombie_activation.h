// Host: faz o ZombieManager ativar (transformar em entidade de verdade) também os zumbis
// perto do jogador do cliente, e não só os perto do jogador local.
//
// A cada frame o ZombieManager (0x0133FF30) pede, em DeterminePerception (0x01321D20), a
// ativação dos zumbis "lab" perto do jogador local, numa fila (0x0133CF70) que 0x0131DCF0
// atende criando a entidade. O hook (no meio desse frame) põe nessa fila os zumbis a até
// 15 m do jogador do cliente. Opcional (em teste): pôr o cliente na lista de pontos de
// interesse, que sozinha não ativou nada. Ver docs/PROGRESSO.md, Zumbis etapa 3.
#pragma once

#include "game/engine.h"

namespace sodcoop::zombie_activation {

// Instala o hook (só no host). Depois de MH_Initialize.
bool installHook(int type, int index);
// Posição do jogador do cliente para o próximo frame; valid = false tira o ponto.
void setClient(bool valid, const engine::Vec3& pos);
// Teste: leva até `max` zumbis "lab" (vagas em uso, a até `from` m e a mais de radius + 3 m de
// center) para um círculo de raio `radius` em volta de center, na altura de center. Devolve
// quantos moveu. Thread principal.
int pullZombies(const engine::Vec3& center, float radius, float from, int max);

}  // namespace sodcoop::zombie_activation
