// Representa o outro jogador como fantoche, a partir do estado da rede.
// Chamar update() uma vez por frame na thread principal.
#pragma once

#include <string>

#include "core/config.h"
#include "net/session.h"

namespace sodcoop::remote_player {

// localInWorld: o jogador local está no mundo (há nível carregado).
void update(const net::RemoteState& remote, bool localInWorld, const std::string& model,
            const AnimConfig& anim);

}  // namespace sodcoop::remote_player
