// Formato dos pacotes UDP do mod. Tudo little-endian (x86), sem ponteiros:
// só IDs estáveis e valores (regra do Co-op Anything).
#pragma once

#include <cstdint>

namespace sodcoop::net {

inline constexpr uint32_t kMagic = 0x43444F53;  // "SODC"
inline constexpr uint16_t kProtocolVersion = 8;  // v2: speed; v3: timeMs; v4: action; v5: weapon; v6: base/upper; v7: aim; v8: aimPose
inline constexpr int kActionNameSize = 48;
inline constexpr int kWeaponPathSize = 128;

enum class PacketType : uint8_t {
    Hello = 1,    // cliente -> host: pede para entrar
    Welcome = 2,  // host -> cliente: aceito
    Reject = 3,   // host -> cliente: recusado (sessão cheia / versão)
    State = 4,    // ambos: estado do jogador local (também serve de keep-alive)
    Bye = 5,      // ambos: saída limpa
};

#pragma pack(push, 1)
struct Header {
    uint32_t magic;
    uint16_t version;
    PacketType type;
    uint8_t reserved;
    uint32_t senderId;  // token aleatório da instância, gerado na inicialização
    uint32_t seq;       // sequência por remetente, para descartar pacotes velhos/duplicados
};

struct HelloPacket {
    Header h;
    char name[32];
};

struct WelcomePacket {
    Header h;
    uint32_t clientId;  // ecoa o senderId do cliente aceito
};

struct RejectPacket {
    Header h;
    uint8_t reason;  // 1 = sessão cheia, 2 = versão incompatível
};

struct StatePacket {
    Header h;
    uint32_t timeMs;  // relógio de alta precisão do remetente (ms), base da interpolação
    uint8_t hasPlayer;  // 0 = no menu/carregando (sem jogador no mundo)
    float pos[3];
    float yaw;
    float speed;  // velocidade horizontal medida pelo remetente (m/s), escolhe a animação
    char action[kActionNameSize];  // animação de ação tocando (ataque, pulo...); vazio = só locomoção
    uint16_t actionSeq;  // muda a cada nova ação (inclusive a mesma repetida)
    char weapon[kWeaponPathSize];  // modelo (.cgf) da arma na mão; vazio = mão vazia
    char base[kActionNameSize];    // postura da camada 0 (idle com arma, mira _AIM_*); vazio = padrão
    char upper[kActionNameSize];   // animação da camada 1 (tiro, recarga, segurar arma); vazio = nenhuma
    uint16_t upperSeq;             // muda a cada animação nova na camada 1
    uint8_t aiming;                // IK de mira ligado (arma levantada)
    float aimOffset[3];            // alvo da mira - posição do jogador (m), dá direção e inclinação
    char aimPose[kActionNameSize]; // animação de aim poses da arma (ex.: AimPoses_shotgun)
};
#pragma pack(pop)

inline constexpr int kMaxPacketSize = 512;

}  // namespace sodcoop::net
