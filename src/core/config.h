// Configuração lida de sodcoop.ini (ao lado do exe do jogo).
#pragma once

#include <string>

#include "game/zombies.h"

#include "core/window_keeper.h"
#include "net/session.h"

namespace sodcoop {

// [anim] Locomoção do fantoche escolhida pela velocidade horizontal recebida.
// Usa os blend spaces (LMG) de locomoção do grafo UL_FullBody do jogador.
struct AnimConfig {
    std::string idle = "Test_hero_idle";  // = relaxed_idle_nw
    std::string walk = "_WALK_NW";
    std::string run = "_RUN_NW";
    std::string sprint = "_SPRINT_NW";
    // Calibrado em 2026-11-01 pelo deslocamento real do jogador no Breakdown:
    // andar ~1,1–1,4 m/s, correr ~3,0–3,7 m/s, sprint ~4,5–5,7 m/s.
    float walkMin = 0.4f;    // m/s: acima disso, andar
    float runMin = 2.2f;     // m/s: acima disso, correr
    float sprintMin = 4.1f;  // m/s: acima disso, sprint
    float hysteresis = 0.3f; // m/s: margem para não alternar na fronteira
    // Velocidade em que cada clipe "encaixa" o pé no chão; a reprodução é
    // escalada por velocidade_real / nominal (limitada a 0,5x–2x).
    float walkSpeed = 1.3f;
    float runSpeed = 3.4f;
    float sprintSpeed = 5.2f;
    // Valor gravado em ISkeletonAnim "AnimationDrivenMotion" do fantoche (-1 = não mexe).
    // Padrão do jogo: 0. Nem 0 nem 1 removem o deslocamento de raiz num AnimObject
    // (2026-11-01); 1 é o usado pelos atores e foi o testado com a compensação abaixo.
    int drivenMotion = 1;
    // Osso usado para compensar o deslocamento de raiz dos clipes (vazio = não compensa).
    // Medido: a corrida leva a bacia até ~3,1 m à frente da entidade a cada ciclo.
    std::string rootBone = "Bip01 Pelvis";
};

struct Config {
    bool enabled = true;           // [general] enabled=0 desliga o mod (jogo 100% vanilla)
    std::string instanceName = "default";  // [general] instance=host|client — log e nome do jogador
    int positionLogIntervalMs = 1000;      // [debug] position_log_interval_ms
    bool keepRunningUnfocused = false;     // [debug] keep_running_unfocused: não pausa/desacelera sem foco
    int maxFps = 60;                       // [debug] max_fps: limite de FPS (sys_MaxFPS); 0 = não mexe
    bool luaCommandFile = true;            // [debug] lua_command_file: executa sodcoop_cmd.lua (teste)
    bool animWatch = true;                 // [debug] anim_watch: registra as animações do jogador local
    // [puppet] modelo do fantoche (jogador remoto) e espelho local de teste
    zombies::Config zombies;               // [zombies] enabled, radius, model
    std::string puppetModel = "characters/male/human/body/tshirt_jeans/variant/v01.cdf";  // model
    bool mirrorLocal = false;              // mirror_local=1: fantoche copia o jogador local (só com role=off)
    float mirrorOffsetX = 2.0f;            // mirror_offset_x (metros, eixo X do mundo)
    float mirrorOffsetY = 0.0f;            // mirror_offset_y
    // [net] role=off|host|client, bind, host, port, send_rate_hz, timeout_ms
    net::SessionConfig net;
    AnimConfig anim;
    // [window] x, y, width, height (width/height 0 = não mexe na janela)
    window_keeper::Rect window;
};

// Diretório do exe do jogo, com barra final (ex.: C:\...\testenv\host\).
std::wstring gameDirectory();

// Lê <gameDirectory>\sodcoop.ini; campos ausentes ficam com o padrão.
Config loadConfig();

}  // namespace sodcoop
