// Carros: cada lado manda o carro em que o jogador local está (classe, posição e
// ângulos, id e nome). O outro lado usa o mesmo carro do mundo dele (mesmo nome e classe,
// vindo do save copiado) ou, sem ele, cria um; sem física, o move junto (interpolado),
// com o fantoche no banco; ao sair, o carro fica estacionado (com física). Quem entra de
// passageiro no carro do outro manda passengerOf/passengerSeat, e o outro lado senta o
// fantoche ao lado do motorista.
// Thread principal.
#pragma once

#include "net/session.h"

namespace sodcoop::vehicles {

// Preenche vehicle/vehiclePos/vehicleAngles do estado local (vazio fora de carro). No
// carro-fantoche do outro jogador, preenche passengerOf/seatOffset (passageiro).
void fillLocal(bool settled, net::PlayerState& s);
// true se o outro jogador está num carro: dirigindo o dele ou de passageiro no meu.
bool remoteSeated(const net::RemoteState& remote);
// Carro do outro jogador: cria, move e remove, e põe o fantoche no banco com a pose de
// dirigir. Devolve true se ele está num carro (aí remote_player não mexe no fantoche).
bool updateRemote(const net::RemoteState& remote, bool settled);
// true uma vez depois que o outro jogador sai do carro: o fantoche deve ser recriado
// (está na pose de volante).
bool needsPuppetRespawn();

}  // namespace sodcoop::vehicles
