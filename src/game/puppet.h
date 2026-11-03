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

// Toca uma animação no fantoche (nome do .chrparams). Sem loop, confere antes
// se o modelo tem a animação. O nome só pode ter letras, dígitos e '_'.
// layer: camada do esqueleto (0 = corpo inteiro; 2 = parte de cima do remetente).
bool playAnimation(const std::string& name, float speed, bool loop = true, float blendSec = 0.25f, int layer = 0);
// Toca a animação parada no tempo normalizado time (0..1), em loop, na camada.
bool holdPose(const std::string& name, float time, int layer);
// Para a animação da camada (blend de saída padrão do motor).
bool stopLayer(int layer);

// Vira corpo mole (ragdoll), em passos: 0 liga a física; 1 (alguns frames depois, e de novo
// se preciso) solta o corpo. Depois disso o fantoche não deve mais ser movido nem animado.
bool ragdoll(int step);

// Avalia uma expressão Lua e devolve o resultado como texto (canal pelo nome
// da entidade "sodcoop_text"). Até ~250 caracteres.
bool readLuaText(const char* expr, std::string& out);

// Prende o modelo (.cgf) no osso weapon_bone do fantoche; "" remove.
// Só aceita caminhos com letras, dígitos e _ / . -
bool setWeapon(const std::string& path);


// Entidade do fantoche atual (pelo id guardado no Lua, não pelo nome); nullptr se não há.
void* entity();
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
