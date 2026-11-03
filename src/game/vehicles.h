// Carros: cada lado manda o carro em que o jogador local está (classe, posição e
// ângulos). O outro lado cria um carro da mesma classe, sem física, e o move junto
// (interpolado); enquanto isso o fantoche do jogador fica escondido. Os carros
// do mundo não são os mesmos nas duas cópias, por isso o carro é criado.
// Thread principal.
#pragma once

#include "net/session.h"

namespace sodcoop::vehicles {

// Preenche vehicle/vehiclePos/vehicleAngles do estado local (vazio fora de carro).
void fillLocal(bool settled, net::PlayerState& s);
// Carro do outro jogador: cria, move e remove, e põe o fantoche no banco com a pose de
// dirigir. Devolve true se ele está num carro (aí remote_player não mexe no fantoche).
bool updateRemote(const net::RemoteState& remote, bool settled);
// true uma vez depois que o outro jogador sai do carro: o fantoche deve ser recriado
// (está na pose de volante).
bool needsPuppetRespawn();

}  // namespace sodcoop::vehicles
