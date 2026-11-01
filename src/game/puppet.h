// Entidade fantoche: personagem sem IA/física/controle, posicionado pelo mod.
// Criado e movido via Lua (System.SpawnEntity / Entity:SetWorldPos), sempre
// na thread principal (chamar só a partir de callbacks de frame).
#pragma once

#include <string>

#include "game/engine.h"

namespace sodcoop::puppet {

// Define as funções Lua auxiliares (idempotente). false se o Lua falhou.
bool ensureLuaHelpers();

// Cria o fantoche se ainda não existir no nível atual.
// drivenMotion: valor de AnimationDrivenMotion do esqueleto (-1 = não mexe).
// Precisa ser aplicado antes de tocar animações: o motor copia o valor para
// cada animação no momento em que ela começa.
bool spawn(const std::string& model, const engine::Vec3& pos, int drivenMotion);

// Move/gira o fantoche (yaw em radianos). false se ele não existe mais
// (ex.: o nível foi recarregado) — o chamador deve chamar spawn de novo.
// rootBone (não vazio): compensa o deslocamento de raiz da animação para que
// esse osso fique horizontalmente sobre pos.
bool move(const engine::Vec3& pos, float yaw, const std::string& rootBone = "");

// Toca uma animação em loop no fantoche (nome do .chrparams, blend de 0,25 s).
bool playAnimation(const std::string& name, float speed);

// Posição atual da entidade do fantoche lida do motor (diagnóstico: detecta
// se algo além do mod moveu a entidade). false se não encontrada.
bool currentPosition(engine::Vec3& out);

// Lê o último valor gravado pelo Lua em SodCoop.Report(a, b, c) (canal Lua -> DLL).
bool readProbe(engine::Vec3& out);

// Ajusta a velocidade de reprodução da animação atual (1 = normal).
bool setAnimationRate(float rate);

// Remove o fantoche do nível (ex.: o outro jogador desconectou).
void remove();

}  // namespace sodcoop::puppet
