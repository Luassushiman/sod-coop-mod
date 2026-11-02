// Diagnóstico: lê as animações que estão tocando no esqueleto de uma entidade
// (fila FIFO de cada camada do CSkeletonAnim) e registra no log quando o
// conjunto muda. Base para replicar ataques e outras ações no fantoche.
#pragma once

#include <string>

#include "game/engine.h"

namespace sodcoop::anim_watch {

// Trilha de animação replicada no fantoche. seq incrementa a cada animação
// nova, inclusive a mesma repetida (tempo voltando ao início).
struct Action {
    char name[48] = {};    // vazio = nada nesta trilha
    uint16_t seq = 0;
};

// Chamar uma vez por frame (thread principal) com a entidade a observar (0 = nenhuma).
// Atualiza a ação atual; com logChanges, registra no log toda mudança nas camadas.
void update(engine::EntityId id, bool logChanges);

// Ação = animação mais nova da camada 0 (corpo inteiro) que não é locomoção nem
// idle: golpes, pulos, quedas, agarrões... Toca uma vez no fantoche.
const Action& currentAction();
// Base = postura da camada 0 que substitui o idle/andar padrão do fantoche:
// idle com arma (relaxed_idle_shotgun...) ou mira (_AIM_*, blend space). Sem seq.
const Action& currentBase();
// Parte de cima = animação mais nova da camada 1 (tiro, recarga, segurar arma).
const Action& currentUpper();
// Aim pose da arma atual (ex.: "AimPoses_shotgun"), vinda do nAnimDirID0 das
// animações da camada 0; vazio se nenhuma tem. Sem seq.
const Action& currentAimPose();

// Animação mais nova da camada 0 de uma entidade qualquer (ex.: zumbi).
// false se a entidade não tem esqueleto ou a camada está vazia.
bool newestLayer0(void* entity, std::string& name, bool& parametric);

// Arquivo do modelo base do personagem (CCharInstance+0x88 -> modelo, caminho em +4;
// é o nome que o motor usa nas mensagens "... for model: %s"). "" se ilegível.
std::string modelPath(void* entity);

}  // namespace sodcoop::anim_watch
