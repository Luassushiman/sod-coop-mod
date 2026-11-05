// Formato dos pacotes UDP do mod. Tudo little-endian (x86), sem ponteiros:
// só IDs estáveis e valores (regra do Co-op Anything).
#pragma once

#include <cstdint>

namespace sodcoop::net {

inline constexpr uint32_t kMagic = 0x43444F53;  // "SODC"
inline constexpr uint16_t kProtocolVersion = 26;  // v2: speed; v3: timeMs; v4: action; v5: weapon; v6: base/upper; v7: aim; v8: aimPose; v9: Zombies; v10: ZombieEntry.model; v11: Hit; v12: Hit host -> cliente (ataque de zumbi); v13: State.dead; v14: ids de zumbi = EntityId (u32); v15: State.timeOfDay; v16: pacote Look; v17: Look com materiais (1200 bytes); v18: State.vehicle; v19: State.vehicleId; v20: State.vehicleName/passengerOf/passengerSeat; v21: pacotes Searched e Container; v22: pacote Give; v23: ZombieEntry.lookHash e pacote ZombieLook; v24: pacotes Locker e Resources; v25: pacote Facilities; v26: pacotes SaveReq e SaveChunk
inline constexpr int kActionNameSize = 48;
inline constexpr int kWeaponPathSize = 128;

enum class PacketType : uint8_t {
    Hello = 1,    // cliente -> host: pede para entrar
    Welcome = 2,  // host -> cliente: aceito
    Reject = 3,   // host -> cliente: recusado (sessão cheia / versão)
    State = 4,    // ambos: estado do jogador local (também serve de keep-alive)
    Bye = 5,      // ambos: saída limpa
    Zombies = 6,  // host -> cliente: zumbis perto dos jogadores (vários pacotes por rodada)
    Hit = 7,      // golpe (enviado 3x). cliente -> host: o cliente acertou um zumbi;
                  // host -> cliente: um zumbi acertou o jogador do cliente
    Look = 8,     // ambos: partes do personagem (aparência do sobrevivente), a cada 2 s
    Searched = 9, // ambos: lugares de busca já vasculhados (bit por nó), a cada 1 s
    Container = 10, // ambos: o que sobrou num lugar vasculhado (ao mudar e a cada 2 s)
    Give = 11,    // ambos: item dado ao outro jogador (enviado 3x)
    ZombieLook = 12, // host -> cliente: partes de um zumbi (ao aparecer e a cada 3 s)
    Locker = 13,  // ambos: vagas do armário da base que mudaram (3x); host: todas a cada 5 s
    Resources = 14, // ambos: os 5 recursos do estoque da base (ao mudar, 3x; host a cada 5 s)
    Facilities = 15, // ambos: instalações das 32 vagas da base (ao mudar, 3x; host a cada 5 s)
    SaveReq = 16,    // cliente -> host: pede o save (e os pedaços que faltam), a cada 250 ms
    SaveChunk = 17,  // host -> cliente: um pedaço do save
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
    uint8_t dead;                  // jogador morto (vida 0): os zumbis do host o soltam
    float timeOfDay;               // hora do dia do remetente (0..24; < 0 = desconhecida); o cliente segue a do host
    char vehicle[32];              // classe do carro em que o remetente está (ex.: Suv_New); vazio = a pé
    float vehiclePos[3];           // posição do carro
    float vehicleAngles[3];        // ângulos do carro (rad)
    uint32_t vehicleId;            // EntityId do carro no remetente (identifica o carro estacionado)
    char vehicleName[32];          // nome da entidade do carro (ex.: ULVEH00): com o mesmo save, o carro
                                   // também existe do outro lado com esse nome (o id muda)
    uint32_t passengerOf;          // passageiro no carro do OUTRO jogador: EntityId do carro lá; 0 = não
    uint8_t passengerSeat;         // assento do passageiro (Suv_New: 3 = ao lado do motorista)
};
inline constexpr int kZombieAnimSize = 32;
inline constexpr int kZombieModelSize = 64;
inline constexpr int kMaxZombiesPerPacket = 10;

struct ZombieEntry {
    uint32_t id;                  // EntityId do zumbi no host (o nome "ULZombie<n>" pode repetir)
    uint8_t flags;                // bit 0: animação em loop; bit 1: morto
    float pos[3];
    float yaw;
    char anim[kZombieAnimSize];   // animação mais nova da camada 0 do zumbi no host
    char model[kZombieModelSize]; // modelo base do personagem no host (.chr)
    uint32_t lookHash;            // hash da aparência (ZombieLookPacket); 0 = ainda sem partes
};

struct ZombiesPacket {
    Header h;
    uint32_t timeMs;  // relógio do host (ms), como StatePacket.timeMs
    uint8_t count;    // entradas válidas em e[]
    ZombieEntry e[kMaxZombiesPerPacket];
};
struct HitPacket {
    Header h;
    uint16_t hitSeq;    // por remetente; o outro lado descarta repetidos
    uint32_t zombieId;  // ZombieEntry.id (alvo ou atacante)
    uint8_t kind;       // 0 = corpo a corpo, 1 = tiro (cliente -> host); 2 = ataque de zumbi (host -> cliente)
    float damage;
};
inline constexpr int kLookSize = 1200;
inline constexpr int kSearchedBytes = 512;  // até 4096 nós (o Breakdown tem 1688)
struct SearchedPacket {
    Header h;
    uint16_t nodeCount;               // nós no mundo do remetente (os dois lados precisam ter o mesmo)
    uint8_t bits[kSearchedBytes];     // bit n = nó n vasculhado
};
inline constexpr int kContainerSlots = 16;
struct ContainerPacket {
    Header h;
    int32_t node;                        // nó do lugar de busca (igual nas duas cópias)
    uint32_t version;                    // maior = mais novo (quem muda soma 1)
    uint32_t count;                      // itens dentro (até 16)
    uint32_t ids[kContainerSlots];       // id do item ou do recurso (mesmos ids nas duas cópias)
    uint8_t counts[kContainerSlots];     // quantidade de cada
    uint8_t types[kContainerSlots];      // 0 = item, 1 = lote de recurso, 2 = reabastecimento
};
struct GivePacket {
    Header h;
    uint16_t giveSeq;   // por remetente; o outro lado descarta repetidos
    uint32_t itemId;    // id do item (tipo << 28 | índice; mesmos ids nas duas cópias)
    uint8_t data[6];    // dados da vaga: quantidade (empilháveis) ou durabilidade/munição (armas)
    uint8_t returned;   // 1 = volta para quem deu (não coube na mochila do outro)
};
struct LookPacket {
    Header h;
    uint16_t lookSeq;       // muda quando a aparência muda
    char parts[kLookSize];  // "anexo=arquivo.chr|material;..." (partes de pele do slot 0)
};
inline constexpr int kLockerPerPacket = 100;
struct LockerEntry {
    uint16_t slot;    // vaga do armário (0..255; igual nas duas cópias, com o mesmo save)
    uint32_t id;      // item (tipo << 28 | índice); 0 = vaga vazia
    uint8_t data[6];  // dados da vaga (quantidade, ou durabilidade/munição)
};
struct LockerPacket {
    Header h;
    uint8_t count;    // entradas válidas
    LockerEntry e[kLockerPerPacket];
};
inline constexpr int kBaseResources = 5;
struct ResourcesPacket {
    Header h;
    float values[kBaseResources];  // na ordem do jogo (resource data tipo 9, entradas 0 a 4)
};
inline constexpr int kBaseSockets = 32;
struct FacilitiesPacket {
    Header h;
    uint32_t facility[kBaseSockets];  // hash do FacilityId de cada vaga (0 = vazia)
    uint32_t variant[kBaseSockets];   // hash do VariantId (em construção, pronta, melhorias)
};
// Save do Breakdown (net/save_transfer.h), em pedaços. O cliente pede; o host responde com
// até kSaveChunksPerReq pedaços que o cliente ainda não marcou em have[].
inline constexpr int kSaveChunkData = 1100;
inline constexpr int kSaveMaxChunks = 1024;  // até ~1,1 MB (o save do Breakdown tem ~200 KB)
inline constexpr int kSaveChunksPerReq = 64;
struct SaveReqPacket {
    Header h;
    uint32_t transferId;                  // do último SaveChunk recebido; 0 = começando
    uint8_t have[kSaveMaxChunks / 8];     // bit n = pedaço n já recebido
};
struct SaveChunkPacket {
    Header h;
    uint32_t transferId;  // muda quando o host monta o pacote de novo
    uint32_t totalSize;   // bytes do pacote inteiro
    uint32_t crc;         // CRC-32 do pacote inteiro
    uint16_t index;       // este pedaço
    uint16_t count;       // total de pedaços
    uint16_t len;         // bytes válidos em data
    uint8_t data[kSaveChunkData];
};
struct ZombieLookPacket {
    Header h;
    uint32_t zombieId;      // ZombieEntry.id
    uint32_t lookHash;      // igual ao ZombieEntry.lookHash enquanto a aparência não muda
    char parts[kLookSize];  // como LookPacket.parts; vai só até o 0 final
};
#pragma pack(pop)

inline constexpr int kMaxPacketSize = 1400;
static_assert(sizeof(ZombiesPacket) <= kMaxPacketSize, "pacote de zumbis maior que o buffer");
static_assert(sizeof(LookPacket) <= kMaxPacketSize, "pacote de aparencia maior que o buffer");
static_assert(sizeof(LockerPacket) <= kMaxPacketSize, "pacote do armario maior que o buffer");
static_assert(sizeof(ZombieLookPacket) <= kMaxPacketSize, "pacote de aparencia de zumbi maior que o buffer");
static_assert(sizeof(SaveChunkPacket) <= 1200, "pedaco do save maior que o limite da Steam");

}  // namespace sodcoop::net
