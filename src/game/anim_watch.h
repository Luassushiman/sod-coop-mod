// Diagnóstico: lê as animações que estão tocando no esqueleto de uma entidade
// (fila FIFO de cada camada do CSkeletonAnim) e registra no log quando o
// conjunto muda. Base para replicar ataques e outras ações no fantoche.
#pragma once

#include "game/engine.h"

namespace sodcoop::anim_watch {

// Ação = animação mais nova da camada 0 (corpo inteiro) que não é locomoção nem
// idle: golpes, pulos, quedas, etc. É o que o fantoche do outro lado repete.
struct Action {
    char name[48] = {};    // vazio = nenhuma ação (só locomoção)
    uint16_t seq = 0;      // incrementa a cada ação nova, inclusive repetida
};

// Chamar uma vez por frame (thread principal) com a entidade a observar (0 = nenhuma).
// Atualiza a ação atual; com logChanges, registra no log toda mudança nas camadas.
void update(engine::EntityId id, bool logChanges);

const Action& currentAction();

}  // namespace sodcoop::anim_watch
