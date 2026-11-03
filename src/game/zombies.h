// Zumbis compartilhados: o host é a autoridade.
//
// Host: a cada 0,1 s lista os zumbis do jogo ("ULZombie<n>") perto dos dois
// jogadores e envia posição, direção e animação (net::setLocalZombies).
// Cliente: desliga os zumbis próprios (zm_disable 1 + zm_killAll) e mostra um
// fantoche (AnimObject) para cada zumbi recebido do host, montado com as partes de
// pele e os materiais do zumbi no host (pacote ZombieLook; sem eles, o modelo
// simplificado `model`).
// Golpes do cliente vão ao host (pacote Hit). A IA do jogo só persegue o jogador do
// host; os zumbis perto do cliente são conduzidos pelo mod no host (ZChase), e os
// golpes deles vão ao cliente, que tira a vida do próprio jogador.
// Tudo na thread principal do jogo.
#pragma once

#include <string>

#include "game/engine.h"

namespace sodcoop::zombies {

struct Config {
    bool enabled = true;
    float radius = 80.0f;  // m em volta de cada jogador
    // Modelo dos fantoches sem a aparência do host (modelo completo simplificado "EigenZombie" do jogo).
    std::string model = "characters/male/zombie/body/tshirt_jeans/low/low.cdf";
    std::string rootBone = "Bip01 Pelvis";
    float meleeDamage = 35.0f;  // por golpe do cliente (Grunt tem 100 de vida)
    float shotDamage = 60.0f;   // por tiro do cliente
    // Perseguição do jogador do cliente (a IA do jogo só persegue o jogador do host).
    bool chase = true;
    float aggroRadius = 15.0f;   // m do cliente para o mod assumir o zumbi (= raio de ativação)
    float attackDamage = 8.0f;   // por golpe de zumbi no jogador do cliente
    // Host: o zumbi conduzido fica sem desenho (DrawSlot 0) e um fantoche com a aparência
    // dele mostra a corrida e o golpe (a animação do zumbi de verdade é do ZombieManager).
    bool hostPuppets = true;
    // Ativar também os zumbis perto do cliente (zombie_activation), e não só os perto do host.
    bool clientActivation = true;
    int clientPointType = -1;   // tipo do ponto na lista do ZombieManager (-1 = nenhum; 1 = aliado)
    int clientPointIndex = 99;  // índice do ponto (aliado: vaga na lista de aliados)
};

enum class AttackKind { Melee = 0, Shot = 1 };

// inWorld: jogador local no mundo. other: posição do outro jogador (se válida).
void updateHost(const Config& cfg, bool inWorld, const engine::Vec3& self, bool otherValid,
                const engine::Vec3& other);
void updateClient(const Config& cfg, bool inWorld, const engine::Vec3& self);

// Cliente: o jogador local deu um golpe/tiro. Escolhe o fantoche de zumbi atingido
// e manda o dano ao host. yaw = direção do corpo; aimDir = direção da mira (tiro).
void onLocalAttack(const Config& cfg, AttackKind kind, const engine::Vec3& self, float yaw,
                   const engine::Vec3& aimDir);

}  // namespace sodcoop::zombies
