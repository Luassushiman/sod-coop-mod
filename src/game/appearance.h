// Aparência do sobrevivente: cada lado manda as partes de pele do próprio jogador
// (anexos "body", "skin", "legs", "head", "hair", "pack"... com o arquivo .chr de cada
// um) e o outro lado troca as partes do fantoche por elas. Thread principal.
#pragma once

#include <string>

#include "game/engine.h"

namespace sodcoop::appearance {

// A cada 2 s lê o esqueleto e as partes do jogador local e os entrega à rede.
void updateLocal(bool settled, engine::EntityId localId);
// Modelo com que o fantoche deve ser criado: o esqueleto do outro jogador, se a
// aparência dele já chegou; senão `fallback`.
std::string puppetModel(const std::string& fallback);
// Chamar depois de remote_player::update. Fantoche recém-criado: prende as partes
// recebidas. Aparência nova num fantoche que já existe: pede outro fantoche (tirar
// partes de um personagem em uso fechou o jogo, numa thread de animação).
void updatePuppet(bool settled);

}  // namespace sodcoop::appearance
