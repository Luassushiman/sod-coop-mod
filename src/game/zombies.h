// Zumbis compartilhados, etapa 1 (só visual): o host é a autoridade.
//
// Host: a cada 0,1 s lista os zumbis do jogo ("ULZombie<n>") perto dos dois
// jogadores e envia posição, direção e animação (net::setLocalZombies).
// Cliente: desliga os zumbis próprios (zm_disable 1 + zm_killAll) e mostra um
// fantoche (AnimObject) para cada zumbi recebido do host.
// Tudo na thread principal do jogo.
#pragma once

#include <string>

#include "game/engine.h"

namespace sodcoop::zombies {

struct Config {
    bool enabled = true;
    float radius = 80.0f;  // m em volta de cada jogador
    // Modelo dos fantoches (modelo completo simplificado "EigenZombie" do jogo).
    std::string model = "characters/male/zombie/body/tshirt_jeans/low/low.cdf";
    std::string rootBone = "Bip01 Pelvis";
};

// inWorld: jogador local no mundo. other: posição do outro jogador (se válida).
void updateHost(const Config& cfg, bool inWorld, const engine::Vec3& self, bool otherValid,
                const engine::Vec3& other);
void updateClient(const Config& cfg, bool inWorld, const engine::Vec3& self);

}  // namespace sodcoop::zombies
