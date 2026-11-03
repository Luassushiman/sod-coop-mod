// Host: faz o ZombieManager ativar (transformar em entidade de verdade) também os zumbis
// perto do jogador do cliente, e não só os perto do jogador local.
//
// A cada frame o ZombieManager (0x0133FF30) monta uma lista de "pontos de interesse"
// (jogador local, aliados que o seguem, carros...) e ativa os zumbis a menos de
// zm_activationDist de um ponto do tipo 0 (jogador) ou 1 (aliado) em DeterminePerception
// (0x01321D20). O hook acrescenta a posição do cliente como mais um aliado, logo depois
// dos aliados do jogo. Ver docs/RE_ENDERECOS.md, 4d.
#pragma once

#include "game/engine.h"

namespace sodcoop::zombie_activation {

// Instala o hook (só no host). Depois de MH_Initialize.
bool installHook(int type, int index);
// Posição do jogador do cliente para o próximo frame; valid = false tira o ponto.
void setClient(bool valid, const engine::Vec3& pos);

}  // namespace sodcoop::zombie_activation
