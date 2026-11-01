// Rastreia o jogador local: hook na função que o motor chama ao definir o
// ator local (publica g_localActorId no Lua) e leitura de posição/rotação.
#pragma once

#include "game/engine.h"

namespace sodcoop::local_player {

// Instala o hook em engine::kSetLocalActor. Exige código já descriptografado.
bool installHook();

// EntityId do jogador local (0 se nenhum).
engine::EntityId currentId();

struct Snapshot {
    engine::EntityId id = 0;
    engine::Vec3 pos{};
    engine::Vec3 angles{};  // radianos (Ang3 do CryEngine)
};

// Lê o estado atual; false se não há jogador local ou a entidade é inválida.
bool sample(Snapshot& out);

}  // namespace sodcoop::local_player
